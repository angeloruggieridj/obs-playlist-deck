#include "doctest/doctest.h"
#include "PlaylistIO.hpp"
using namespace pld;

TEST_CASE("json round-trip") {
    std::vector<PlaylistItem> in = {{"/a/x.mp4", "X"}, {"/b/y.mov", "Y"}};
    std::string text = io::toJson("My List", in);
    std::string name;
    std::vector<PlaylistItem> out;
    REQUIRE(io::fromJson(text, name, out).ok);
    CHECK(name == "My List");
    CHECK(out == in);
}

TEST_CASE("json missing title defaults to stem") {
    std::string text = R"({"version":1,"name":"n","items":[{"path":"/a/Clip 1.mp4"}]})";
    std::string name;
    std::vector<PlaylistItem> out;
    REQUIRE(io::fromJson(text, name, out).ok);
    CHECK(out.size() == 1);
    CHECK(out[0].title == "Clip 1");
}

TEST_CASE("json invalid returns false") {
    std::string name;
    std::vector<PlaylistItem> out;
    CHECK_FALSE(io::fromJson("not json", name, out).ok);
    CHECK_FALSE(io::fromJson(R"({"items":3})", name, out).ok);
}

TEST_CASE("m3u parse handles EXTINF, comments, crlf, spaces") {
    std::string text =
        "#EXTM3U\r\n"
        "#EXTINF:12,Intro Clip\r\n"
        "/videos/intro clip.mp4\r\n"
        "\r\n"
        "# a comment\n"
        "/videos/no-info.mov\n";
    auto items = io::parseM3u(text);
    REQUIRE(items.size() == 2);
    CHECK(items[0].path == "/videos/intro clip.mp4");
    CHECK(items[0].title == "Intro Clip");
    CHECK(items[0].durationMs == 12000); // EXTINF seconds -> ms
    CHECK(items[1].path == "/videos/no-info.mov");
    CHECK(items[1].title == "no-info"); // stem fallback
}

TEST_CASE("json round-trip preserves duration") {
    std::vector<PlaylistItem> in = {{"/a/x.mp4", "X", 90000}, {"/b/y.mov", "Y", -1}};
    std::string text = io::toJson("L", in);
    std::string name;
    std::vector<PlaylistItem> out;
    REQUIRE(io::fromJson(text, name, out).ok);
    CHECK(out == in);
    CHECK(out[0].durationMs == 90000);
}

TEST_CASE("m3u write then parse round-trips paths") {
    std::vector<PlaylistItem> in = {{"/a/x.mp4", "X title"}, {"/b/y.mov", "y"}};
    std::string text = io::toM3u(in);
    auto out = io::parseM3u(text);
    REQUIRE(out.size() == 2);
    CHECK(out[0].path == "/a/x.mp4");
    CHECK(out[0].title == "X title");
    CHECK(out[1].path == "/b/y.mov");
}

// F-13: a playlist file with one broken entry among good ones used to be
// rejected whole — the user lost 200 items over 1. The bad entry is skipped and
// counted so the dock can say so.
TEST_CASE("json skips malformed items instead of failing the file") {
    std::string text = R"({"version":1,"items":[
        {"path":"/a/good.mp4","title":"Good"},
        {"path":123},
        "not an object",
        {"title":"no path"},
        {"path":""},
        {"path":"/b/also-good.mov"}
    ]})";
    std::string name;
    std::vector<PlaylistItem> out;
    auto res = io::fromJson(text, name, out);
    CHECK(res.ok);
    CHECK(res.skipped == 4);
    REQUIRE(out.size() == 2);
    CHECK(out[0].path == "/a/good.mp4");
    CHECK(out[1].path == "/b/also-good.mov");
}

TEST_CASE("structural damage still fails the whole file") {
    std::string name;
    std::vector<PlaylistItem> out;
    CHECK_FALSE(io::fromJson("", name, out).ok);
    CHECK_FALSE(io::fromJson("[1,2,3]", name, out).ok);
    CHECK_FALSE(io::fromJson(R"({"name":"x"})", name, out).ok); // no items array
}

// F-29: nlohmann's value<long long>() returns the default for a float, so a
// duration written as 90500.0 by another tool was silently dropped.
TEST_CASE("json accepts fractional durations by rounding them") {
    std::string text = R"({"items":[{"path":"/a.mp4","duration":90500.4},
                                    {"path":"/b.mp4","duration":1000},
                                    {"path":"/c.mp4","duration":"nonsense"}]})";
    std::string name;
    std::vector<PlaylistItem> out;
    REQUIRE(io::fromJson(text, name, out).ok);
    REQUIRE(out.size() == 3);
    CHECK(out[0].durationMs == 90500);
    CHECK(out[1].durationMs == 1000);
    CHECK(out[2].durationMs == -1);
}

// F-12: .m3u files written next to their media carry relative paths — the
// portable layout every other player produces. Read as absolute, every item
// showed up as "file not found".
TEST_CASE("m3u resolves relative paths against the playlist folder") {
    std::string text =
        "#EXTM3U\n"
        "#EXTINF:5,One\n"
        "media/one.mp4\n"
        "#EXTINF:5,Two\n"
        "..\\shared\\two.mov\n"
        "/absolute/three.mp4\n"
        "C:\\abs\\four.mp4\n"
        "https://example.com/stream.m3u8\n";
    auto items = io::parseM3u(text, "/gigs/friday");
    REQUIRE(items.size() == 5);
    CHECK(items[0].path == "/gigs/friday/media/one.mp4");
    CHECK(items[1].path == "/gigs/friday/..\\shared\\two.mov");
    CHECK(items[2].path == "/absolute/three.mp4"); // absolute: untouched
    CHECK(items[3].path == "C:\\abs\\four.mp4");   // drive path: untouched
    CHECK(items[4].path == "https://example.com/stream.m3u8"); // URL: untouched
}

TEST_CASE("no base directory leaves relative paths alone") {
    auto items = io::parseM3u("#EXTM3U\nmedia/one.mp4\n");
    REQUIRE(items.size() == 1);
    CHECK(items[0].path == "media/one.mp4");
}

TEST_CASE("json resolves relative paths against the playlist folder") {
    std::string text = R"({"items":[{"path":"clips/a.mp4"},{"path":"/abs/b.mp4"}]})";
    std::string name;
    std::vector<PlaylistItem> out;
    REQUIRE(io::fromJson(text, name, out, "/sets/one").ok);
    CHECK(out[0].path == "/sets/one/clips/a.mp4");
    CHECK(out[0].title == "a"); // stem taken from the resolved path
    CHECK(out[1].path == "/abs/b.mp4");
}

// M-8: a "gig folder" (playlist + media) has to survive being copied to another
// machine, which it only does when the paths inside it are relative.
TEST_CASE("portable export writes paths relative to the playlist folder") {
    std::vector<PlaylistItem> in = {{"/sets/one/clips/a.mp4", "A", 1000},
                                    {"/elsewhere/b.mp4", "B", -1}};
    const std::string json = io::toJson("set", in, "/sets/one", true);
    CHECK(json.find("clips/a.mp4") != std::string::npos);
    CHECK(json.find("/sets/one/clips/a.mp4") == std::string::npos);
    CHECK(json.find("/elsewhere/b.mp4") != std::string::npos); // outside: absolute

    const std::string m3u = io::toM3u(in, "/sets/one", true);
    CHECK(m3u.find("\nclips/a.mp4\n") != std::string::npos);

    // And it round-trips: reading it back against the same folder restores the
    // absolute paths.
    auto back = io::parseM3u(m3u, "/sets/one");
    REQUIRE(back.size() == 2);
    CHECK(back[0].path == "/sets/one/clips/a.mp4");
    CHECK(back[1].path == "/elsewhere/b.mp4");
}

TEST_CASE("csv export quotes fields and reports unknown durations") {
    std::vector<PlaylistItem> in = {{"/a/x.mp4", "Say \"hi\"", 1500}, {"/b/y.mov", "Y", -1}};
    const std::string csv = io::toCsv(in);
    CHECK(csv.rfind("title,path,duration_ms\n", 0) == 0);
    CHECK(csv.find("\"Say \"\"hi\"\"\",\"/a/x.mp4\",1500") != std::string::npos);
    CHECK(csv.find("\"Y\",\"/b/y.mov\",-1") != std::string::npos);
}

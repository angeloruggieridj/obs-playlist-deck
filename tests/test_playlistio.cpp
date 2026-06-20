#include "doctest/doctest.h"
#include "PlaylistIO.hpp"
using namespace pld;

TEST_CASE("json round-trip") {
    std::vector<PlaylistItem> in = {{"/a/x.mp4", "X"}, {"/b/y.mov", "Y"}};
    std::string text = io::toJson("My List", in);
    std::string name;
    std::vector<PlaylistItem> out;
    REQUIRE(io::fromJson(text, name, out));
    CHECK(name == "My List");
    CHECK(out == in);
}

TEST_CASE("json missing title defaults to stem") {
    std::string text = R"({"version":1,"name":"n","items":[{"path":"/a/Clip 1.mp4"}]})";
    std::string name;
    std::vector<PlaylistItem> out;
    REQUIRE(io::fromJson(text, name, out));
    CHECK(out.size() == 1);
    CHECK(out[0].title == "Clip 1");
}

TEST_CASE("json invalid returns false") {
    std::string name;
    std::vector<PlaylistItem> out;
    CHECK_FALSE(io::fromJson("not json", name, out));
    CHECK_FALSE(io::fromJson(R"({"items":3})", name, out));
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
    REQUIRE(io::fromJson(text, name, out));
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

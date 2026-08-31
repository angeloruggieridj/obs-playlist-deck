#include "doctest/doctest.h"
#include "MediaPath.hpp"
using namespace pld::mediapath;

TEST_CASE("extensionLower") {
    CHECK(extensionLower("/x/Clip.MP4") == "mp4");
    CHECK(extensionLower("a.tar.gz") == "gz");
    CHECK(extensionLower("noext") == "");
    CHECK(extensionLower("/dir.with.dot/file") == "");
}

TEST_CASE("isMediaFile") {
    CHECK(isMediaFile("a.mp4"));
    CHECK(isMediaFile("A.MOV"));
    CHECK(isMediaFile("song.flac"));
    CHECK_FALSE(isMediaFile("note.txt"));
    CHECK_FALSE(isMediaFile("noext"));
}

TEST_CASE("fileStem") {
    CHECK(fileStem("/a/b/Clip 1.mp4") == "Clip 1");
    CHECK(fileStem("C:\\v\\x.mov") == "x");
    CHECK(fileStem("plain.mp4") == "plain");
    CHECK(fileStem("noext") == "noext");
}

TEST_CASE("fileName keeps the extension") {
    CHECK(fileName("/a/b/Clip 1.mp4") == "Clip 1.mp4");
    CHECK(fileName("C:\\v\\x.mov") == "x.mov");
    CHECK(fileName("plain.mp4") == "plain.mp4");
}

TEST_CASE("isAbsolute knows both platforms' shapes") {
    CHECK(isAbsolute("/videos/a.mp4"));
    CHECK(isAbsolute("~/a.mp4"));
    CHECK(isAbsolute("C:\\videos\\a.mp4"));
    CHECK(isAbsolute("c:/videos/a.mp4"));
    CHECK(isAbsolute("\\\\nas\\share\\a.mp4"));
    CHECK_FALSE(isAbsolute("media/a.mp4"));
    CHECK_FALSE(isAbsolute("..\\media\\a.mp4"));
    CHECK_FALSE(isAbsolute(""));
    CHECK_FALSE(isAbsolute("C:"));
}

TEST_CASE("isUrl does not mistake a drive path for a scheme") {
    CHECK(isUrl("https://example.com/a.m3u8"));
    CHECK(isUrl("rtmp://host/live"));
    CHECK_FALSE(isUrl("C:\\videos\\a.mp4"));
    CHECK_FALSE(isUrl("/videos/a.mp4"));
    CHECK_FALSE(isUrl("a b://x"));
}

TEST_CASE("resolveAgainst only touches relative paths") {
    CHECK(resolveAgainst("/gigs/friday", "media/a.mp4") == "/gigs/friday/media/a.mp4");
    CHECK(resolveAgainst("/gigs/friday/", "a.mp4") == "/gigs/friday/a.mp4");
    CHECK(resolveAgainst("/gigs", "/abs/a.mp4") == "/abs/a.mp4");
    CHECK(resolveAgainst("", "media/a.mp4") == "media/a.mp4");
    CHECK(resolveAgainst("/gigs", "https://h/a.m3u8") == "https://h/a.m3u8");
}

TEST_CASE("relativeTo strips the folder when the file lives inside it") {
    CHECK(relativeTo("/gigs/friday", "/gigs/friday/media/a.mp4") == "media/a.mp4");
    CHECK(relativeTo("C:/Gigs/Friday", "c:\\gigs\\friday\\a.mp4") == "a.mp4");
    CHECK(relativeTo("/gigs/friday", "/gigs/saturday/a.mp4") == "/gigs/saturday/a.mp4");
    CHECK(relativeTo("/gigs/fri", "/gigs/friday/a.mp4") == "/gigs/friday/a.mp4");
    CHECK(relativeTo("", "/a.mp4") == "/a.mp4");
}

TEST_CASE("naturalLess reads numbers the way people do") {
    CHECK(naturalLess("clip2.mp4", "clip10.mp4"));
    CHECK_FALSE(naturalLess("clip10.mp4", "clip2.mp4"));
    CHECK(naturalLess("clip002.mp4", "clip10.mp4")); // leading zeros ignored
    CHECK(naturalLess("Intro", "outro"));            // case-insensitive
    CHECK(naturalLess("a", "ab"));
    CHECK_FALSE(naturalLess("a", "a"));
    CHECK(naturalLess("track 9 - end", "track 10 - start"));
}

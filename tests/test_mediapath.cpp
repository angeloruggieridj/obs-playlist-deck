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

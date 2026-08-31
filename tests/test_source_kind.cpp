#include "doctest/doctest.h"
#include "SourceKind.hpp"
using namespace pld;

// F-1: the controller drove both source types by writing local_file, but the
// VLC source in OBS has no such setting — it reads a "playlist" array of
// {value: path} objects. Selecting an item with a VLC source bound therefore
// never changed what played. The kind is what decides which of the two settings
// the controller writes.
TEST_CASE("source ids map to the settings shape they actually use") {
    CHECK(sourceKindFromId("ffmpeg_source") == SourceKind::Ffmpeg);
    CHECK(sourceKindFromId("vlc_source") == SourceKind::Vlc);
    CHECK(sourceKindFromId("image_source") == SourceKind::Unsupported);
    CHECK(sourceKindFromId("") == SourceKind::Unsupported);
    CHECK(sourceKindFromId(nullptr) == SourceKind::Unsupported);
}

TEST_CASE("only the two media sources are bindable") {
    CHECK(isBindableSourceId("ffmpeg_source"));
    CHECK(isBindableSourceId("vlc_source"));
    CHECK_FALSE(isBindableSourceId("browser_source"));
    CHECK_FALSE(isBindableSourceId(nullptr));
}

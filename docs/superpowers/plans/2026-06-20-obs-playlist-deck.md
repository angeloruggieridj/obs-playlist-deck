# obs-playlist-deck Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A native OBS Studio dock plugin that drives an existing OBS media source's file from a managed playlist, cross-platform (Windows, Linux, macOS universal2).

**Architecture:** A pure-C++17 `core/` library (Playlist model, JSON+M3U IO, media-path helpers) with no OBS/Qt deps, fully unit-tested with doctest; a thin `obs/` glue layer wrapping libobs; a `ui/` Qt6 dock. CI builds all platforms and gates on the core tests.

**Tech Stack:** C++17, CMake ≥3.22, Qt6 (Widgets), libobs + obs-frontend-api, doctest (vendored), nlohmann/json (vendored), GitHub Actions.

## Global Constraints

- C++ standard: C++17, `CMAKE_CXX_STANDARD_REQUIRED ON`.
- `src/core/` MUST NOT include any OBS or Qt header. Only the standard library and `third_party/nlohmann/json.hpp`.
- Plugin module name / id: `obs-playlist-deck`. Dock id: `obs-playlist-deck-dock`. Display name: `Playlist Deck`.
- macOS artifact MUST be universal2 (`lipo -archs` → `x86_64 arm64`) and contain only `@rpath` Qt references.
- Bound media source ids: `ffmpeg_source` and `vlc_source`.
- Vendored pins: doctest v2.4.11, nlohmann/json v3.11.3.
- No browser source, HTTP server, HTML, web/URL sources, or volume slider.
- Commits: conventional style; no Claude/AI attribution in commit messages or PR/repo content.

---

### Task 1: Repo scaffold, vendored headers, core CMake + smoke test

**Files:**
- Create: `.gitignore`, `LICENSE`, `README.md`, `CMakeLists.txt`
- Create: `third_party/doctest/doctest.h` (fetched), `third_party/nlohmann/json.hpp` (fetched)
- Create: `src/core/CMakeLists.txt`, `src/core/Playlist.hpp` (minimal), `src/core/Playlist.cpp` (minimal)
- Create: `tests/CMakeLists.txt`, `tests/test_main.cpp`, `tests/test_smoke.cpp`

**Interfaces:**
- Produces: a buildable `playlist-deck-core` static lib and a `playlist-deck-tests` CTest executable.

- [ ] **Step 1: Create directory layout and fetch vendored single-headers**

```bash
mkdir -p src/core src/obs src/ui third_party/doctest third_party/nlohmann tests data/locale .github/workflows
curl -fsSL https://github.com/doctest/doctest/releases/download/v2.4.11/doctest.h -o third_party/doctest/doctest.h
curl -fsSL https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp -o third_party/nlohmann/json.hpp
```

- [ ] **Step 2: Write `.gitignore`**

```
build/
.cache/
*.user
.DS_Store
out/
```

- [ ] **Step 3: Root `CMakeLists.txt`** (core + tests always; plugin only when OBS found — added in Task 8)

```cmake
cmake_minimum_required(VERSION 3.22)
project(obs-playlist-deck VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_PLUGIN "Build the OBS plugin (needs libobs + Qt6)" ON)
option(BUILD_TESTS "Build unit tests" ON)

add_subdirectory(src/core)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(BUILD_PLUGIN)
  add_subdirectory(src/plugin)  # added in Task 8
endif()
```

> Note: comment out the `src/plugin` line until Task 8 creates it, OR guard with `if(EXISTS ...)`. Use:
> `if(BUILD_PLUGIN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/CMakeLists.txt")`.

- [ ] **Step 4: `src/core/CMakeLists.txt`**

```cmake
add_library(playlist-deck-core STATIC
  Playlist.cpp
  MediaPath.cpp
  PlaylistIO.cpp
)
target_include_directories(playlist-deck-core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC ${CMAKE_SOURCE_DIR}/third_party/nlohmann
)
target_compile_features(playlist-deck-core PUBLIC cxx_std_17)
```

> `MediaPath.cpp` and `PlaylistIO.cpp` are created in Tasks 2-4. For this task, create empty `.cpp`/`.hpp` stubs so it compiles, or list only `Playlist.cpp` now and add the others in their tasks. Use the stub approach: create empty `MediaPath.cpp`/`PlaylistIO.cpp` with `// stub`.

- [ ] **Step 5: Minimal `src/core/Playlist.hpp`**

```cpp
#pragma once
#include <string>
#include <vector>

namespace pld {

struct PlaylistItem {
    std::string path;
    std::string title;
};

inline bool operator==(const PlaylistItem& a, const PlaylistItem& b) {
    return a.path == b.path && a.title == b.title;
}

class Playlist {
public:
    const std::vector<PlaylistItem>& items() const { return items_; }
    int size() const { return static_cast<int>(items_.size()); }
    bool empty() const { return items_.empty(); }
    int currentIndex() const { return current_; }

private:
    std::vector<PlaylistItem> items_;
    int current_ = -1;
};

} // namespace pld
```

- [ ] **Step 6: `src/core/Playlist.cpp`**

```cpp
#include "Playlist.hpp"
// Method bodies added in Task 2.
```

- [ ] **Step 7: `tests/test_main.cpp`** (doctest entry point)

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
```

- [ ] **Step 8: `tests/test_smoke.cpp`**

```cpp
#include "doctest/doctest.h"
#include "Playlist.hpp"

TEST_CASE("empty playlist has no current item") {
    pld::Playlist p;
    CHECK(p.empty());
    CHECK(p.size() == 0);
    CHECK(p.currentIndex() == -1);
}
```

- [ ] **Step 9: `tests/CMakeLists.txt`**

```cmake
add_executable(playlist-deck-tests
  test_main.cpp
  test_smoke.cpp
  test_playlist.cpp
  test_mediapath.cpp
  test_playlistio.cpp
)
target_link_libraries(playlist-deck-tests PRIVATE playlist-deck-core)
target_include_directories(playlist-deck-tests PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
add_test(NAME core COMMAND playlist-deck-tests)
```

> Create empty `test_playlist.cpp`, `test_mediapath.cpp`, `test_playlistio.cpp` now (filled in later tasks) so it links.

- [ ] **Step 10: Configure, build, run**

Run:
```bash
cmake -B build -DBUILD_PLUGIN=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: `core` test PASS (1 assertion in smoke).

- [ ] **Step 11: Commit**

```bash
git add -A
git commit -m "chore: scaffold core lib, vendored doctest/json, test harness"
```

---

### Task 2: Playlist model (mutations + navigation)

**Files:**
- Modify: `src/core/Playlist.hpp`, `src/core/Playlist.cpp`
- Test: `tests/test_playlist.cpp`

**Interfaces:**
- Produces:
  - `void add(const PlaylistItem&)`
  - `bool insert(int index, const PlaylistItem&)` (index in [0,size])
  - `bool removeAt(int index)`
  - `void clear()`
  - `bool move(int from, int to)`
  - `bool moveUp(int index)` / `bool moveDown(int index)`
  - `bool setCurrent(int index)` (index in [0,size) or -1)
  - `const PlaylistItem* current() const` (nullptr if none)
  - `int next(bool wrap)` / `int prev(bool wrap)` (returns new index or -1)
  - `void setItems(std::vector<PlaylistItem>)` (resets current to -1)

**Semantics (locked):**
- `removeAt`: after removal — empty→current=-1; index<current→current--; index==current→current=min(current,size-1); index>current→unchanged.
- `next`: empty→-1 (unchanged); current==-1→0; current+1<size→++current; at last→ wrap?0:-1 (unchanged when -1).
- `prev`: empty→-1; current==-1→size-1; current-1>=0→--current; at first→ wrap?size-1:-1.
- `move(from,to)`: both in range; removes from `from`, inserts at `to`; `current` follows the moved item if it was current, otherwise stays pointing at the same logical item.

- [ ] **Step 1: Write failing tests** `tests/test_playlist.cpp`

```cpp
#include "doctest/doctest.h"
#include "Playlist.hpp"
using namespace pld;

static PlaylistItem mk(const std::string& p) { return PlaylistItem{p, p}; }

TEST_CASE("add appends items") {
    Playlist p;
    p.add(mk("a")); p.add(mk("b"));
    CHECK(p.size() == 2);
    CHECK(p.items()[0].path == "a");
    CHECK(p.currentIndex() == -1);
}

TEST_CASE("setCurrent and current()") {
    Playlist p; p.add(mk("a")); p.add(mk("b"));
    CHECK(p.setCurrent(1));
    CHECK(p.currentIndex() == 1);
    CHECK(p.current() != nullptr);
    CHECK(p.current()->path == "b");
    CHECK_FALSE(p.setCurrent(5));
    CHECK(p.setCurrent(-1));
    CHECK(p.current() == nullptr);
}

TEST_CASE("removeAt adjusts current") {
    Playlist p; for (auto c : {"a","b","c"}) p.add(mk(c));
    p.setCurrent(2);
    CHECK(p.removeAt(0));        // remove before current
    CHECK(p.currentIndex() == 1);
    CHECK(p.current()->path == "c");
    p.setCurrent(1);
    CHECK(p.removeAt(1));        // remove the current (last)
    CHECK(p.currentIndex() == 0);
    p.removeAt(0);
    CHECK(p.empty());
    CHECK(p.currentIndex() == -1);
}

TEST_CASE("next/prev with and without wrap") {
    Playlist p; for (auto c : {"a","b"}) p.add(mk(c));
    CHECK(p.next(false) == 0);
    CHECK(p.next(false) == 1);
    CHECK(p.next(false) == -1);          // at end, no wrap
    CHECK(p.currentIndex() == 1);        // unchanged
    CHECK(p.next(true) == 0);            // wrap
    CHECK(p.prev(false) == -1);          // at first, no wrap
    CHECK(p.prev(true) == 1);            // wrap to last
}

TEST_CASE("move reorders and tracks current") {
    Playlist p; for (auto c : {"a","b","c"}) p.add(mk(c));
    p.setCurrent(0);                     // current = "a"
    CHECK(p.move(0, 2));                 // a -> end: [b,c,a]
    CHECK(p.items()[2].path == "a");
    CHECK(p.current()->path == "a");     // current followed
    CHECK(p.moveUp(0) == false);         // already top
    CHECK(p.moveDown(2) == false);       // already bottom
}

TEST_CASE("setItems resets current") {
    Playlist p; p.add(mk("a")); p.setCurrent(0);
    p.setItems({mk("x"), mk("y")});
    CHECK(p.size() == 2);
    CHECK(p.currentIndex() == -1);
}
```

- [ ] **Step 2: Run, verify FAIL**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: FAIL (methods missing → compile error).

- [ ] **Step 3: Implement in `Playlist.hpp`** (declare) and `Playlist.cpp` (define)

Header additions (inside class, public):
```cpp
    const PlaylistItem* current() const {
        return (current_ >= 0 && current_ < size()) ? &items_[current_] : nullptr;
    }
    void add(const PlaylistItem& it);
    bool insert(int index, const PlaylistItem& it);
    bool removeAt(int index);
    void clear();
    bool move(int from, int to);
    bool moveUp(int index);
    bool moveDown(int index);
    bool setCurrent(int index);
    int next(bool wrap);
    int prev(bool wrap);
    void setItems(std::vector<PlaylistItem> items);
```

`Playlist.cpp`:
```cpp
#include "Playlist.hpp"
namespace pld {

void Playlist::add(const PlaylistItem& it) { items_.push_back(it); }

bool Playlist::insert(int index, const PlaylistItem& it) {
    if (index < 0 || index > size()) return false;
    items_.insert(items_.begin() + index, it);
    if (current_ >= index) ++current_;
    return true;
}

bool Playlist::removeAt(int index) {
    if (index < 0 || index >= size()) return false;
    items_.erase(items_.begin() + index);
    if (empty()) { current_ = -1; }
    else if (index < current_) { --current_; }
    else if (index == current_) { if (current_ > size() - 1) current_ = size() - 1; }
    return true;
}

void Playlist::clear() { items_.clear(); current_ = -1; }

bool Playlist::move(int from, int to) {
    if (from < 0 || from >= size() || to < 0 || to >= size()) return false;
    if (from == to) return true;
    PlaylistItem it = items_[from];
    items_.erase(items_.begin() + from);
    items_.insert(items_.begin() + to, it);
    if (current_ == from) current_ = to;
    else {
        if (from < current_) --current_;
        if (to <= current_) ++current_;
    }
    return true;
}

bool Playlist::moveUp(int index) { return index > 0 && move(index, index - 1); }
bool Playlist::moveDown(int index) { return index >= 0 && index < size() - 1 && move(index, index + 1); }

bool Playlist::setCurrent(int index) {
    if (index == -1) { current_ = -1; return true; }
    if (index < 0 || index >= size()) return false;
    current_ = index; return true;
}

int Playlist::next(bool wrap) {
    if (empty()) return -1;
    if (current_ == -1) { current_ = 0; return current_; }
    if (current_ + 1 < size()) { ++current_; return current_; }
    if (wrap) { current_ = 0; return current_; }
    return -1;
}

int Playlist::prev(bool wrap) {
    if (empty()) return -1;
    if (current_ == -1) { current_ = size() - 1; return current_; }
    if (current_ - 1 >= 0) { --current_; return current_; }
    if (wrap) { current_ = size() - 1; return current_; }
    return -1;
}

void Playlist::setItems(std::vector<PlaylistItem> items) {
    items_ = std::move(items); current_ = -1;
}

} // namespace pld
```

- [ ] **Step 4: Run, verify PASS**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "feat(core): playlist model with navigation and reorder"
```

---

### Task 3: MediaPath helpers

**Files:**
- Create: `src/core/MediaPath.hpp`; overwrite `src/core/MediaPath.cpp`
- Test: `tests/test_mediapath.cpp`

**Interfaces:**
- Produces (namespace `pld::mediapath`):
  - `std::string extensionLower(const std::string& path)` — lowercase ext without dot, or "".
  - `bool isMediaFile(const std::string& path)` — true if ext in the supported set.
  - `std::string fileStem(const std::string& path)` — base filename without dir or extension.

- [ ] **Step 1: Failing tests** `tests/test_mediapath.cpp`

```cpp
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
```

- [ ] **Step 2: Run, verify FAIL** — `cmake --build build` → compile error.

- [ ] **Step 3: `src/core/MediaPath.hpp`**

```cpp
#pragma once
#include <string>
namespace pld::mediapath {
std::string extensionLower(const std::string& path);
bool isMediaFile(const std::string& path);
std::string fileStem(const std::string& path);
}
```

- [ ] **Step 4: `src/core/MediaPath.cpp`**

```cpp
#include "MediaPath.hpp"
#include <algorithm>
#include <array>

namespace pld::mediapath {

static size_t lastSep(const std::string& p) {
    size_t a = p.find_last_of('/');
    size_t b = p.find_last_of('\\');
    if (a == std::string::npos) return b;
    if (b == std::string::npos) return a;
    return std::max(a, b);
}

std::string extensionLower(const std::string& path) {
    size_t sep = lastSep(path);
    std::string base = (sep == std::string::npos) ? path : path.substr(sep + 1);
    size_t dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= base.size()) return "";
    std::string ext = base.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return ext;
}

bool isMediaFile(const std::string& path) {
    static const std::array<const char*, 19> kExts = {
        "mp4","mov","mkv","avi","webm","m4v","mpg","mpeg","ts","flv","wmv",
        "mp3","m4a","aac","wav","flac","ogg","opus","3gp"
    };
    std::string ext = extensionLower(path);
    if (ext.empty()) return false;
    return std::any_of(kExts.begin(), kExts.end(),
                       [&](const char* e){ return ext == e; });
}

std::string fileStem(const std::string& path) {
    size_t sep = lastSep(path);
    std::string base = (sep == std::string::npos) ? path : path.substr(sep + 1);
    size_t dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return base;
    return base.substr(0, dot);
}

} // namespace pld::mediapath
```

- [ ] **Step 5: Run, verify PASS.**
- [ ] **Step 6: Commit** — `git add -A && git commit -m "feat(core): media path helpers"`

---

### Task 4: PlaylistIO — JSON and M3U

**Files:**
- Create: `src/core/PlaylistIO.hpp`; overwrite `src/core/PlaylistIO.cpp`
- Test: `tests/test_playlistio.cpp`

**Interfaces:**
- Produces (namespace `pld::io`):
  - `std::string toJson(const std::string& name, const std::vector<PlaylistItem>&)`
  - `bool fromJson(const std::string& text, std::string& nameOut, std::vector<PlaylistItem>& itemsOut)` — false on parse error.
  - `std::string toM3u(const std::vector<PlaylistItem>&)`
  - `std::vector<PlaylistItem> parseM3u(const std::string& text)` — empty title → filename stem.

**Locked formats:** see spec. JSON `{version:1,name,items:[{path,title}]}`; titles default to `mediapath::fileStem`.

- [ ] **Step 1: Failing tests** `tests/test_playlistio.cpp`

```cpp
#include "doctest/doctest.h"
#include "PlaylistIO.hpp"
using namespace pld;

TEST_CASE("json round-trip") {
    std::vector<PlaylistItem> in = {{"/a/x.mp4","X"},{"/b/y.mov","Y"}};
    std::string text = io::toJson("My List", in);
    std::string name; std::vector<PlaylistItem> out;
    REQUIRE(io::fromJson(text, name, out));
    CHECK(name == "My List");
    CHECK(out == in);
}

TEST_CASE("json missing title defaults to stem") {
    std::string text = R"({"version":1,"name":"n","items":[{"path":"/a/Clip 1.mp4"}]})";
    std::string name; std::vector<PlaylistItem> out;
    REQUIRE(io::fromJson(text, name, out));
    CHECK(out.size() == 1);
    CHECK(out[0].title == "Clip 1");
}

TEST_CASE("json invalid returns false") {
    std::string name; std::vector<PlaylistItem> out;
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
    CHECK(items[1].path == "/videos/no-info.mov");
    CHECK(items[1].title == "no-info");      // stem fallback
}

TEST_CASE("m3u write then parse round-trips paths") {
    std::vector<PlaylistItem> in = {{"/a/x.mp4","X title"},{"/b/y.mov","y"}};
    std::string text = io::toM3u(in);
    auto out = io::parseM3u(text);
    REQUIRE(out.size() == 2);
    CHECK(out[0].path == "/a/x.mp4");
    CHECK(out[0].title == "X title");
    CHECK(out[1].path == "/b/y.mov");
}
```

- [ ] **Step 2: Run, verify FAIL.**

- [ ] **Step 3: `src/core/PlaylistIO.hpp`**

```cpp
#pragma once
#include <string>
#include <vector>
#include "Playlist.hpp"
namespace pld::io {
std::string toJson(const std::string& name, const std::vector<PlaylistItem>& items);
bool fromJson(const std::string& text, std::string& nameOut, std::vector<PlaylistItem>& itemsOut);
std::string toM3u(const std::vector<PlaylistItem>& items);
std::vector<PlaylistItem> parseM3u(const std::string& text);
}
```

- [ ] **Step 4: `src/core/PlaylistIO.cpp`**

```cpp
#include "PlaylistIO.hpp"
#include "MediaPath.hpp"
#include <nlohmann/json.hpp>
#include <sstream>

using nlohmann::json;
namespace pld::io {

std::string toJson(const std::string& name, const std::vector<PlaylistItem>& items) {
    json j;
    j["version"] = 1;
    j["name"] = name;
    j["items"] = json::array();
    for (const auto& it : items)
        j["items"].push_back({{"path", it.path}, {"title", it.title}});
    return j.dump(2);
}

bool fromJson(const std::string& text, std::string& nameOut, std::vector<PlaylistItem>& itemsOut) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    if (!j.contains("items") || !j["items"].is_array()) return false;
    nameOut = j.value("name", std::string{});
    itemsOut.clear();
    for (const auto& e : j["items"]) {
        if (!e.is_object() || !e.contains("path") || !e["path"].is_string()) return false;
        PlaylistItem it;
        it.path = e["path"].get<std::string>();
        it.title = e.value("title", std::string{});
        if (it.title.empty()) it.title = mediapath::fileStem(it.path);
        itemsOut.push_back(std::move(it));
    }
    return true;
}

std::string toM3u(const std::vector<PlaylistItem>& items) {
    std::ostringstream os;
    os << "#EXTM3U\n";
    for (const auto& it : items) {
        os << "#EXTINF:-1," << it.title << "\n";
        os << it.path << "\n";
    }
    return os.str();
}

static std::string trimCR(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

std::vector<PlaylistItem> parseM3u(const std::string& text) {
    std::vector<PlaylistItem> items;
    std::istringstream is(text);
    std::string line;
    std::string pendingTitle;
    bool haveTitle = false;
    while (std::getline(is, line)) {
        line = trimCR(line);
        if (line.empty()) continue;
        if (line.rfind("#EXTINF:", 0) == 0) {
            size_t comma = line.find(',');
            pendingTitle = (comma == std::string::npos) ? "" : line.substr(comma + 1);
            haveTitle = true;
            continue;
        }
        if (line[0] == '#') continue;     // comment / directive
        PlaylistItem it;
        it.path = line;
        it.title = (haveTitle && !pendingTitle.empty()) ? pendingTitle
                                                         : mediapath::fileStem(line);
        items.push_back(std::move(it));
        pendingTitle.clear();
        haveTitle = false;
    }
    return items;
}

} // namespace pld::io
```

- [ ] **Step 5: Run, verify PASS.**
- [ ] **Step 6: Commit** — `git add -A && git commit -m "feat(core): JSON and M3U playlist IO"`

---

### Task 5: OBS media-source glue

**Files:**
- Create: `src/plugin/MediaSourceController.hpp`, `src/plugin/MediaSourceController.cpp`

**Interfaces:**
- Produces class `MediaSourceController` (uses libobs; not unit-tested — verified via build + manual smoke):
  - `static std::vector<std::string> listMediaSources();` — names of `ffmpeg_source`/`vlc_source`.
  - `void bind(const std::string& sourceName);` / `void unbind();`
  - `bool setFileAndRestart(const std::string& path);`
  - `void playPause();` `void stop();` `void restart();`
  - `void setOnMediaEnded(std::function<void()> cb);` — invoked (queued to Qt thread) on `media_ended`.

- [ ] **Step 1: Header `src/plugin/MediaSourceController.hpp`**

```cpp
#pragma once
#include <functional>
#include <string>
#include <vector>
#include <obs.h>

class MediaSourceController {
public:
    ~MediaSourceController();
    static std::vector<std::string> listMediaSources();

    void bind(const std::string& sourceName);
    void unbind();
    bool isBound() const { return source_ != nullptr; }
    std::string boundName() const { return boundName_; }

    bool setFileAndRestart(const std::string& path);
    void playPause();
    void stop();
    void restart();

    void setOnMediaEnded(std::function<void()> cb) { onEnded_ = std::move(cb); }

private:
    static void mediaEndedThunk(void* data, calldata_t* cd);

    obs_source_t* source_ = nullptr;  // strong ref while bound
    std::string boundName_;
    std::function<void()> onEnded_;
};
```

- [ ] **Step 2: Implementation `src/plugin/MediaSourceController.cpp`**

```cpp
#include "MediaSourceController.hpp"
#include <util/threading.h>

static bool isMediaId(const char* id) {
    return id && (strcmp(id, "ffmpeg_source") == 0 || strcmp(id, "vlc_source") == 0);
}

std::vector<std::string> MediaSourceController::listMediaSources() {
    std::vector<std::string> out;
    auto cb = [](void* param, obs_source_t* src) -> bool {
        const char* id = obs_source_get_id(src);
        if (isMediaId(id)) {
            const char* name = obs_source_get_name(src);
            if (name) static_cast<std::vector<std::string>*>(param)->emplace_back(name);
        }
        return true;
    };
    obs_enum_sources(cb, &out);
    return out;
}

MediaSourceController::~MediaSourceController() { unbind(); }

void MediaSourceController::bind(const std::string& sourceName) {
    unbind();
    obs_source_t* s = obs_get_source_by_name(sourceName.c_str());
    if (!s) return;
    source_ = s;                     // keep strong ref
    boundName_ = sourceName;
    signal_handler_t* sh = obs_source_get_signal_handler(source_);
    if (sh) signal_handler_connect(sh, "media_ended", &MediaSourceController::mediaEndedThunk, this);
}

void MediaSourceController::unbind() {
    if (source_) {
        signal_handler_t* sh = obs_source_get_signal_handler(source_);
        if (sh) signal_handler_disconnect(sh, "media_ended", &MediaSourceController::mediaEndedThunk, this);
        obs_source_release(source_);
        source_ = nullptr;
    }
    boundName_.clear();
}

bool MediaSourceController::setFileAndRestart(const std::string& path) {
    if (!source_) return false;
    obs_data_t* settings = obs_source_get_settings(source_);
    obs_data_set_bool(settings, "is_local_file", true);
    obs_data_set_string(settings, "local_file", path.c_str());
    obs_source_update(source_, settings);
    obs_data_release(settings);
    obs_source_media_restart(source_);
    return true;
}

void MediaSourceController::playPause() { if (source_) obs_source_media_play_pause(source_, false); }
void MediaSourceController::stop()      { if (source_) obs_source_media_stop(source_); }
void MediaSourceController::restart()   { if (source_) obs_source_media_restart(source_); }

void MediaSourceController::mediaEndedThunk(void* data, calldata_t*) {
    auto* self = static_cast<MediaSourceController*>(data);
    if (self && self->onEnded_) self->onEnded_();  // UI marshals to Qt thread (Task 6)
}
```

> Note: `obs_source_media_play_pause(src, false)` toggles toward playing; the UI tracks state to pass the right bool. Keep simple: expose `playPause()` that calls with `false` (resume/play). Pause uses `true`. The UI in Task 6 calls `obs_source_media_play_pause` indirectly via two methods — adjust to `play()`/`pause()` if needed; for the dock we expose play (false) and a separate pause (true). Add `void pause()` calling `obs_source_media_play_pause(source_, true)`.

- [ ] **Step 3: Add `void pause();`** to header and impl: `if (source_) obs_source_media_play_pause(source_, true);`

- [ ] **Step 4: Commit** (compiles only with the plugin target wired in Task 8; commit now as glue)

```bash
git add -A && git commit -m "feat(obs): media source controller (bind, set file, transport, media_ended)"
```

---

### Task 6: Qt dock widget (UI + wiring)

**Files:**
- Create: `src/plugin/PlaylistDock.hpp`, `src/plugin/PlaylistDock.cpp`

**Interfaces:**
- Produces `class PlaylistDock : public QDockWidget`. Holds a `pld::Playlist`, a `MediaSourceController`, and registers hotkeys (Task 7 merges here). Public ctor `explicit PlaylistDock(QWidget* parent=nullptr)`.

**UI elements:** source dropdown + refresh button; playlist `QListWidget`; buttons Add files / Remove / Up / Down / Clear; transport Play / Pause / Stop / Prev / Next; wrap checkbox; named-playlist combo + Save/Load/Delete; Import / Export buttons; status `QLabel`.

**Behavior:**
- Double-click or "Play selected" → `selectAndPlay(row)`: `playlist_.setCurrent(row)`, highlight row, `controller_.setFileAndRestart(path)`.
- `media_ended` callback → `QMetaObject::invokeMethod(this, "advance", Qt::QueuedConnection)`.
- `advance()` → `int i = playlist_.next(wrap_); if (i>=0) selectAndPlay(i); else controller_.stop();`
- Source dropdown change → `controller_.bind(name)`.
- Refresh on `OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED` / `SOURCE_*` via the frontend callback registered in plugin-main (Task 8), which calls `dock->refreshSources()`.

- [ ] **Step 1: Header `src/plugin/PlaylistDock.hpp`**

```cpp
#pragma once
#include <QDockWidget>
#include <obs.h>
#include "Playlist.hpp"
#include "MediaSourceController.hpp"

class QListWidget; class QComboBox; class QLabel; class QCheckBox; class QPushButton;

class PlaylistDock : public QDockWidget {
    Q_OBJECT
public:
    explicit PlaylistDock(QWidget* parent = nullptr);
    ~PlaylistDock() override;

    void refreshSources();

public slots:
    void advance();          // next or stop (used by media_ended)

private slots:
    void onAddFiles();
    void onRemove();
    void onUp();
    void onDown();
    void onClear();
    void onPlaySelected();
    void onPlay();
    void onPause();
    void onStop();
    void onPrev();
    void onNext();
    void onSourceChanged(int index);
    void onSavePlaylist();
    void onLoadPlaylist();
    void onDeletePlaylist();
    void onImport();
    void onExport();

private:
    void buildUi();
    void rebuildList();
    void selectAndPlay(int row);
    void setStatus(const QString& msg, bool error = false);
    std::string configDir() const;            // obs_module_config_path("playlists")
    void refreshPlaylistCombo();

    pld::Playlist playlist_;
    MediaSourceController controller_;
    bool wrap_ = true;

    QComboBox*  sourceCombo_   = nullptr;
    QListWidget* list_         = nullptr;
    QComboBox*  playlistCombo_ = nullptr;
    QCheckBox*  wrapCheck_     = nullptr;
    QLabel*     status_        = nullptr;
};
```

- [ ] **Step 2: Implementation `src/plugin/PlaylistDock.cpp`** — full code

```cpp
#include "PlaylistDock.hpp"
#include "PlaylistIO.hpp"
#include "MediaPath.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QFileDialog>
#include <QInputDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QWidget>

using namespace pld;

PlaylistDock::PlaylistDock(QWidget* parent) : QDockWidget(parent) {
    setWindowTitle(obs_module_text("PlaylistDeck"));
    setObjectName("obs-playlist-deck-dock");
    buildUi();
    refreshSources();
    refreshPlaylistCombo();
    controller_.setOnMediaEnded([this]() {
        QMetaObject::invokeMethod(this, "advance", Qt::QueuedConnection);
    });
}

PlaylistDock::~PlaylistDock() { controller_.setOnMediaEnded(nullptr); controller_.unbind(); }

static QPushButton* btn(const char* text) { return new QPushButton(QString::fromUtf8(text)); }

void PlaylistDock::buildUi() {
    auto* root = new QWidget(this);
    auto* col = new QVBoxLayout(root);

    auto* srcRow = new QHBoxLayout();
    sourceCombo_ = new QComboBox();
    auto* refreshBtn = btn("Refresh");
    srcRow->addWidget(new QLabel("Media source:"));
    srcRow->addWidget(sourceCombo_, 1);
    srcRow->addWidget(refreshBtn);
    col->addLayout(srcRow);

    list_ = new QListWidget();
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    col->addWidget(list_, 1);

    auto* editRow = new QHBoxLayout();
    auto* addBtn = btn("Add files"); auto* rmBtn = btn("Remove");
    auto* upBtn = btn("Up"); auto* downBtn = btn("Down"); auto* clrBtn = btn("Clear");
    for (auto* b : {addBtn, rmBtn, upBtn, downBtn, clrBtn}) editRow->addWidget(b);
    col->addLayout(editRow);

    auto* trRow = new QHBoxLayout();
    auto* playSelBtn = btn("Play selected");
    auto* playBtn = btn("Play"); auto* pauseBtn = btn("Pause"); auto* stopBtn = btn("Stop");
    auto* prevBtn = btn("Prev"); auto* nextBtn = btn("Next");
    for (auto* b : {playSelBtn, playBtn, pauseBtn, stopBtn, prevBtn, nextBtn}) trRow->addWidget(b);
    col->addLayout(trRow);

    wrapCheck_ = new QCheckBox("Loop playlist (wrap)");
    wrapCheck_->setChecked(wrap_);
    col->addWidget(wrapCheck_);

    auto* plRow = new QHBoxLayout();
    playlistCombo_ = new QComboBox();
    auto* saveBtn = btn("Save"); auto* loadBtn = btn("Load"); auto* delBtn = btn("Delete");
    auto* impBtn = btn("Import"); auto* expBtn = btn("Export");
    plRow->addWidget(new QLabel("Playlists:"));
    plRow->addWidget(playlistCombo_, 1);
    for (auto* b : {saveBtn, loadBtn, delBtn, impBtn, expBtn}) plRow->addWidget(b);
    col->addLayout(plRow);

    status_ = new QLabel("");
    col->addWidget(status_);

    setWidget(root);

    connect(refreshBtn, &QPushButton::clicked, this, &PlaylistDock::refreshSources);
    connect(addBtn,  &QPushButton::clicked, this, &PlaylistDock::onAddFiles);
    connect(rmBtn,   &QPushButton::clicked, this, &PlaylistDock::onRemove);
    connect(upBtn,   &QPushButton::clicked, this, &PlaylistDock::onUp);
    connect(downBtn, &QPushButton::clicked, this, &PlaylistDock::onDown);
    connect(clrBtn,  &QPushButton::clicked, this, &PlaylistDock::onClear);
    connect(playSelBtn, &QPushButton::clicked, this, &PlaylistDock::onPlaySelected);
    connect(playBtn,  &QPushButton::clicked, this, &PlaylistDock::onPlay);
    connect(pauseBtn, &QPushButton::clicked, this, &PlaylistDock::onPause);
    connect(stopBtn,  &QPushButton::clicked, this, &PlaylistDock::onStop);
    connect(prevBtn,  &QPushButton::clicked, this, &PlaylistDock::onPrev);
    connect(nextBtn,  &QPushButton::clicked, this, &PlaylistDock::onNext);
    connect(saveBtn,  &QPushButton::clicked, this, &PlaylistDock::onSavePlaylist);
    connect(loadBtn,  &QPushButton::clicked, this, &PlaylistDock::onLoadPlaylist);
    connect(delBtn,   &QPushButton::clicked, this, &PlaylistDock::onDeletePlaylist);
    connect(impBtn,   &QPushButton::clicked, this, &PlaylistDock::onImport);
    connect(expBtn,   &QPushButton::clicked, this, &PlaylistDock::onExport);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*){ onPlaySelected(); });
    connect(sourceCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &PlaylistDock::onSourceChanged);
    connect(wrapCheck_, &QCheckBox::toggled, this, [this](bool v){ wrap_ = v; });
}

void PlaylistDock::rebuildList() {
    list_->clear();
    for (int i = 0; i < playlist_.size(); ++i) {
        const auto& it = playlist_.items()[i];
        auto* item = new QListWidgetItem(QString::fromStdString(it.title));
        item->setToolTip(QString::fromStdString(it.path));
        if (i == playlist_.currentIndex())
            item->setText("▶ " + item->text());
        list_->addItem(item);
    }
    if (playlist_.currentIndex() >= 0)
        list_->setCurrentRow(playlist_.currentIndex());
}

void PlaylistDock::selectAndPlay(int row) {
    if (!playlist_.setCurrent(row)) return;
    const auto* it = playlist_.current();
    if (!it) return;
    if (!controller_.isBound()) { setStatus("No media source bound.", true); return; }
    if (controller_.setFileAndRestart(it->path))
        setStatus(QString("Playing: %1").arg(QString::fromStdString(it->title)));
    else
        setStatus("Failed to set media source.", true);
    rebuildList();
}

void PlaylistDock::advance() {
    int i = playlist_.next(wrap_);
    if (i >= 0) selectAndPlay(i);
    else { controller_.stop(); setStatus("Playlist finished."); }
}

void PlaylistDock::onAddFiles() {
    QStringList files = QFileDialog::getOpenFileNames(this, "Add media files");
    for (const auto& f : files) {
        std::string p = f.toStdString();
        if (!mediapath::isMediaFile(p)) continue;
        playlist_.add(PlaylistItem{p, mediapath::fileStem(p)});
    }
    rebuildList();
}

void PlaylistDock::onRemove() {
    int row = list_->currentRow();
    if (row >= 0) { playlist_.removeAt(row); rebuildList(); }
}
void PlaylistDock::onUp()   { int r=list_->currentRow(); if (playlist_.moveUp(r)) { rebuildList(); list_->setCurrentRow(r-1);} }
void PlaylistDock::onDown() { int r=list_->currentRow(); if (playlist_.moveDown(r)) { rebuildList(); list_->setCurrentRow(r+1);} }
void PlaylistDock::onClear(){ playlist_.clear(); rebuildList(); }
void PlaylistDock::onPlaySelected() { int r=list_->currentRow(); if (r>=0) selectAndPlay(r); }
void PlaylistDock::onPlay()  { controller_.playPause(); }
void PlaylistDock::onPause() { controller_.pause(); }
void PlaylistDock::onStop()  { controller_.stop(); }
void PlaylistDock::onPrev()  { int i=playlist_.prev(wrap_); if (i>=0) selectAndPlay(i); }
void PlaylistDock::onNext()  { advance(); }

void PlaylistDock::onSourceChanged(int) {
    QString name = sourceCombo_->currentText();
    if (name.isEmpty()) { controller_.unbind(); return; }
    controller_.bind(name.toStdString());
    setStatus(QString("Bound to: %1").arg(name));
}

void PlaylistDock::refreshSources() {
    QString prev = sourceCombo_ ? sourceCombo_->currentText() : QString();
    sourceCombo_->blockSignals(true);
    sourceCombo_->clear();
    for (const auto& n : MediaSourceController::listMediaSources())
        sourceCombo_->addItem(QString::fromStdString(n));
    int idx = sourceCombo_->findText(prev);
    if (idx >= 0) sourceCombo_->setCurrentIndex(idx);
    sourceCombo_->blockSignals(false);
    if (idx < 0) onSourceChanged(sourceCombo_->currentIndex());
}

std::string PlaylistDock::configDir() const {
    char* p = obs_module_config_path("playlists");
    std::string s = p ? p : "";
    bfree(p);
    if (!s.empty()) QDir().mkpath(QString::fromStdString(s));
    return s;
}

void PlaylistDock::refreshPlaylistCombo() {
    playlistCombo_->clear();
    QDir dir(QString::fromStdString(configDir()));
    for (const auto& fi : dir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Name))
        playlistCombo_->addItem(fi.completeBaseName());
}

void PlaylistDock::onSavePlaylist() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "Save playlist", "Name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;
    std::string text = io::toJson(name.toStdString(), playlist_.items());
    QString path = QString::fromStdString(configDir()) + "/" + name + ".json";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { f.write(text.c_str()); f.close(); }
    refreshPlaylistCombo();
    setStatus(QString("Saved playlist: %1").arg(name));
}

void PlaylistDock::onLoadPlaylist() {
    QString name = playlistCombo_->currentText();
    if (name.isEmpty()) return;
    QString path = QString::fromStdString(configDir()) + "/" + name + ".json";
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { setStatus("Cannot open playlist.", true); return; }
    std::string text = f.readAll().toStdString(); f.close();
    std::string n; std::vector<PlaylistItem> items;
    if (!io::fromJson(text, n, items)) { setStatus("Invalid playlist file.", true); return; }
    playlist_.setItems(std::move(items));
    rebuildList();
    setStatus(QString("Loaded playlist: %1").arg(name));
}

void PlaylistDock::onDeletePlaylist() {
    QString name = playlistCombo_->currentText();
    if (name.isEmpty()) return;
    QFile::remove(QString::fromStdString(configDir()) + "/" + name + ".json");
    refreshPlaylistCombo();
    setStatus(QString("Deleted playlist: %1").arg(name));
}

void PlaylistDock::onImport() {
    QString path = QFileDialog::getOpenFileName(this, "Import playlist", "",
                       "Playlists (*.json *.m3u *.m3u8)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { setStatus("Cannot open file.", true); return; }
    std::string text = f.readAll().toStdString(); f.close();
    std::vector<PlaylistItem> items;
    if (path.endsWith(".json", Qt::CaseInsensitive)) {
        std::string n;
        if (!io::fromJson(text, n, items)) { setStatus("Invalid JSON playlist.", true); return; }
    } else {
        items = io::parseM3u(text);
    }
    playlist_.setItems(std::move(items));
    rebuildList();
    setStatus("Imported playlist.");
}

void PlaylistDock::onExport() {
    QString path = QFileDialog::getSaveFileName(this, "Export playlist", "",
                       "JSON (*.json);;M3U (*.m3u)");
    if (path.isEmpty()) return;
    std::string text = path.endsWith(".m3u", Qt::CaseInsensitive)
        ? io::toM3u(playlist_.items())
        : io::toJson("playlist", playlist_.items());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { f.write(text.c_str()); f.close(); }
    setStatus("Exported playlist.");
}

void PlaylistDock::setStatus(const QString& msg, bool error) {
    status_->setStyleSheet(error ? "color:#e06c75;" : "");
    status_->setText(msg);
}
```

- [ ] **Step 3: Commit** — `git add -A && git commit -m "feat(ui): playlist dock widget"`

---

### Task 7: Hotkeys (merge into PlaylistDock)

**Files:**
- Modify: `src/plugin/PlaylistDock.hpp`, `src/plugin/PlaylistDock.cpp`

**Interfaces:**
- Adds `registerHotkeys()` / `unregisterHotkeys()` and members `obs_hotkey_id` for next/prev/play-pause/stop. Called from ctor/dtor.

- [ ] **Step 1: Header additions** (private)

```cpp
    void registerHotkeys();
    void unregisterHotkeys();
    obs_hotkey_id hkNext_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkPrev_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkPlayPause_ = OBS_INVALID_HOTKEY_ID;
    obs_hotkey_id hkStop_ = OBS_INVALID_HOTKEY_ID;
```

- [ ] **Step 2: Implementation** (append to `PlaylistDock.cpp`, call `registerHotkeys()` at end of ctor, `unregisterHotkeys()` at start of dtor)

```cpp
void PlaylistDock::registerHotkeys() {
    auto reg = [this](const char* id, const char* desc, void (PlaylistDock::*fn)()) {
        return obs_hotkey_register_frontend(id, desc,
            [](void* data, obs_hotkey_id, obs_hotkey_t*, bool pressed) {
                if (!pressed) return;
                auto* pair = static_cast<std::pair<PlaylistDock*, void (PlaylistDock::*)()>*>(data);
                PlaylistDock* self = pair->first;
                auto m = pair->second;
                QMetaObject::invokeMethod(self, [self, m]() { (self->*m)(); }, Qt::QueuedConnection);
            },
            new std::pair<PlaylistDock*, void (PlaylistDock::*)()>(this, fn));
    };
    hkNext_      = reg("obs-playlist-deck.next",  "Playlist Deck: Next",       &PlaylistDock::onNext);
    hkPrev_      = reg("obs-playlist-deck.prev",  "Playlist Deck: Previous",   &PlaylistDock::onPrev);
    hkPlayPause_ = reg("obs-playlist-deck.playpause", "Playlist Deck: Play/Pause", &PlaylistDock::onPlay);
    hkStop_      = reg("obs-playlist-deck.stop",  "Playlist Deck: Stop",       &PlaylistDock::onStop);
}

void PlaylistDock::unregisterHotkeys() {
    for (auto id : {hkNext_, hkPrev_, hkPlayPause_, hkStop_})
        if (id != OBS_INVALID_HOTKEY_ID) obs_hotkey_unregister(id);
}
```

> The `std::pair` allocations leak by design for plugin lifetime (one-time, freed by process exit); acceptable for hotkey callbacks per OBS examples. Add `#include <utility>`.

- [ ] **Step 3: Commit** — `git add -A && git commit -m "feat(ui): OBS frontend hotkeys for transport"`

---

### Task 8: plugin-main + plugin CMake + data/locale

**Files:**
- Create: `src/plugin/plugin-main.cpp`, `src/plugin/CMakeLists.txt`
- Create: `data/locale/en-US.ini`
- Modify: root `CMakeLists.txt` (enable `src/plugin` subdir)

**Interfaces:** registers the dock on `OBS_FRONTEND_EVENT_FINISHED_LOADING`; wires source-list refresh.

- [ ] **Step 1: `src/plugin/plugin-main.cpp`**

```cpp
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include "PlaylistDock.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-playlist-deck", "en-US")

MODULE_EXPORT const char* obs_module_description(void) {
    return "Playlist Deck - drive an OBS media source from a native playlist dock";
}
MODULE_EXPORT const char* obs_module_name(void) { return "Playlist Deck"; }

static PlaylistDock* g_dock = nullptr;
static constexpr const char* DOCK_ID = "obs-playlist-deck-dock";

static void on_frontend_event(enum obs_frontend_event event, void*) {
    switch (event) {
    case OBS_FRONTEND_EVENT_FINISHED_LOADING: {
        auto* mw = static_cast<QMainWindow*>(obs_frontend_get_main_window());
        g_dock = new PlaylistDock(mw);
        obs_frontend_add_custom_qdock(DOCK_ID, g_dock);
        break;
    }
    case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
    case OBS_FRONTEND_EVENT_SCENE_CHANGED:
    case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
        if (g_dock) g_dock->refreshSources();
        break;
    default: break;
    }
}

bool obs_module_load(void) {
    blog(LOG_INFO, "[obs-playlist-deck] loaded");
    obs_frontend_add_event_callback(on_frontend_event, nullptr);
    return true;
}
void obs_module_unload(void) { g_dock = nullptr; }
```

- [ ] **Step 2: `data/locale/en-US.ini`**

```ini
PlaylistDeck="Playlist Deck"
```

- [ ] **Step 3: `src/plugin/CMakeLists.txt`**

```cmake
find_package(libobs QUIET)
if(NOT libobs_FOUND)
  find_package(LibObs REQUIRED)
endif()
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

set(CMAKE_AUTOMOC ON)

add_library(obs-playlist-deck MODULE
  plugin-main.cpp
  PlaylistDock.cpp
  MediaSourceController.cpp
)
target_link_libraries(obs-playlist-deck PRIVATE
  playlist-deck-core
  OBS::libobs
  OBS::obs-frontend-api
  Qt6::Core
  Qt6::Widgets
)
target_include_directories(obs-playlist-deck PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src/core
)

if(WIN32)
  set(OBS_PLUGIN_DESTINATION "obs-plugins/64bit")
  set(OBS_DATA_DESTINATION   "data/obs-plugins/obs-playlist-deck")
elseif(APPLE)
  set(OBS_PLUGIN_DESTINATION "obs-plugins")
  set(OBS_DATA_DESTINATION   "obs-plugins/obs-playlist-deck")
else()
  include(GNUInstallDirs)
  set(OBS_PLUGIN_DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
  set(OBS_DATA_DESTINATION   "${CMAKE_INSTALL_DATAROOTDIR}/obs/obs-plugins/obs-playlist-deck")
endif()

install(TARGETS obs-playlist-deck LIBRARY DESTINATION "${OBS_PLUGIN_DESTINATION}")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/data/" DESTINATION "${OBS_DATA_DESTINATION}")
```

- [ ] **Step 4: Root CMake — confirm `src/plugin` subdir is enabled** (the `if(EXISTS ...)` guard from Task 1 now resolves true).

- [ ] **Step 5: Commit** — `git add -A && git commit -m "feat: plugin entry point, plugin CMake, locale"`

---

### Task 9: CI workflow (tests gate + Linux + Windows + macOS universal + release)

**Files:**
- Create: `.github/workflows/build_project.yml`

**Interfaces:** jobs `tests`, `linux`, `windows`, `macos`, `release`. Packaging jobs `needs: tests`.

- [ ] **Step 1: Write the workflow** — full file

```yaml
name: Build Plugin
on:
  push: { branches: [main], tags: ["v*"] }
  pull_request: { branches: [main] }
permissions: { contents: write }
env:
  OBS_VERSION: "32.1.2"
  OBS_DEPS_VERSION: "2025-08-23"

jobs:
  tests:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v6
      - run: sudo apt-get update && sudo apt-get install -y cmake ninja-build
      - run: cmake -B build -S . -G Ninja -DBUILD_PLUGIN=OFF -DBUILD_TESTS=ON
      - run: cmake --build build
      - run: ctest --test-dir build --output-on-failure

  linux:
    needs: tests
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v6
      - run: sudo apt-get update && sudo apt-get install -y libobs-dev qt6-base-dev cmake ninja-build
      - run: cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DCMAKE_PREFIX_PATH="/usr"
      - run: cmake --build build
      - run: DESTDIR="${{ github.workspace }}/release" cmake --install build
      - run: tar -czf obs-playlist-deck-linux-x86_64.tar.gz -C "${{ github.workspace }}/release" .
      - uses: actions/upload-artifact@v7
        with: { name: obs-playlist-deck-linux, path: obs-playlist-deck-linux-x86_64.tar.gz }

  windows:
    needs: tests
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v6
      - name: Install Qt6
        shell: powershell
        run: |
          python -m pip install --upgrade pip
          python -m pip install "aqtinstall==3.1.*" "py7zr<1.0"
          python -m aqt install-qt windows desktop 6.7.3 win64_msvc2019_64 -O C:\Qt
          "Qt6_DIR=C:\Qt\6.7.3\msvc2019_64" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
          "C:\Qt\6.7.3\msvc2019_64\bin" | Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
      - name: Download OBS deps
        env: { GH_TOKEN: "${{ github.token }}" }
        shell: powershell
        run: |
          New-Item -ItemType Directory -Path C:\deps-raw,C:\obs-deps -Force
          gh release download --repo obsproject/obs-deps --pattern "windows-deps-[0-9]*-x64*.zip" --dir C:\deps-raw
          $zip = (Get-ChildItem "C:\deps-raw" -Filter "*.zip" | Select-Object -First 1).FullName
          Expand-Archive $zip -DestinationPath C:\obs-deps-extract
          $top = Get-ChildItem "C:\obs-deps-extract"
          if ($top.Count -eq 1 -and $top[0].PSIsContainer) { Get-ChildItem $top[0].FullName | Move-Item -Destination C:\obs-deps }
          else { $top | Move-Item -Destination C:\obs-deps }
      - name: Build OBS dev
        shell: cmd
        run: |
          curl -L "https://github.com/obsproject/obs-studio/archive/refs/tags/%OBS_VERSION%.zip" -o obs.zip
          tar -xf obs.zip -C C:\
          cmake -B C:\obs-build -S "C:\obs-studio-%OBS_VERSION%" -A x64 ^
            -DENABLE_PLUGINS=OFF -DENABLE_SCRIPTING=OFF -DENABLE_BROWSER=OFF -DENABLE_UPDATER=OFF ^
            -DDepsPath="C:\obs-deps" -DFFMPEG_PATH="C:\obs-deps" -DQTDIR="%Qt6_DIR%" ^
            -DCMAKE_PREFIX_PATH="C:\obs-deps;%Qt6_DIR%"
          cmake --build C:\obs-build --target obs-frontend-api --config Release --parallel
          cmake --install C:\obs-build --config Release --component Development --prefix C:\obs-dev
      - name: Build plugin
        shell: cmd
        run: |
          cmake -B build -S . -A x64 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF ^
            -DCMAKE_PREFIX_PATH="C:\obs-dev;%Qt6_DIR%;C:\obs-deps"
          cmake --build build --config Release
          cmake --install build --config Release --prefix "${{ github.workspace }}\release"
      - run: Compress-Archive -Path "${{ github.workspace }}\release\*" -DestinationPath obs-playlist-deck-windows.zip
      - uses: actions/upload-artifact@v7
        with: { name: obs-playlist-deck-windows, path: obs-playlist-deck-windows.zip }

  macos:
    needs: tests
    runs-on: macos-15
    steps:
      - uses: actions/checkout@v6
      - run: brew install cmake ninja
      - name: Download universal OBS deps
        run: |
          mkdir -p /tmp/obs-deps /tmp/obs-deps-qt6
          curl -fsSL "https://github.com/obsproject/obs-deps/releases/download/${OBS_DEPS_VERSION}/macos-deps-${OBS_DEPS_VERSION}-universal.tar.xz" | tar -xJ -C /tmp/obs-deps
          curl -fsSL "https://github.com/obsproject/obs-deps/releases/download/${OBS_DEPS_VERSION}/macos-deps-qt6-${OBS_DEPS_VERSION}-universal.tar.xz" | tar -xJ -C /tmp/obs-deps-qt6
      - name: Build OBS dev (universal, no Metal)
        run: |
          git clone --depth=1 --branch ${OBS_VERSION} https://github.com/obsproject/obs-studio.git /tmp/obs
          sed -i.bak 's/^\( *\)add_subdirectory(libobs-metal)/\1# disabled for dev build/' /tmp/obs/CMakeLists.txt
          cmake -B /tmp/obs-build -S /tmp/obs -G Xcode \
            -DENABLE_PLUGINS=OFF -DENABLE_SCRIPTING=OFF -DENABLE_BROWSER=OFF -DENABLE_VLC=OFF \
            -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
            -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
            -DCMAKE_PREFIX_PATH="/tmp/obs-deps-qt6;/tmp/obs-deps"
          cmake --build /tmp/obs-build --target obs-frontend-api --config Release --parallel
          cmake --install /tmp/obs-build --config Release --component Development --prefix /tmp/obs-dev
      - name: Build plugin (universal)
        run: |
          cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF \
            -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
            -Dlibobs_DIR="/tmp/obs-dev/Frameworks/libobs.framework/Resources/cmake" \
            -Dobs-frontend-api_DIR="/tmp/obs-dev/lib/cmake/obs-frontend-api" \
            -DQt6_DIR="/tmp/obs-deps-qt6/lib/cmake/Qt6" \
            -DCMAKE_PREFIX_PATH="/tmp/obs-dev;/tmp/obs-deps-qt6;/tmp/obs-deps"
          cmake --build build
          DESTDIR="${{ github.workspace }}/release" cmake --install build
      - name: Package .plugin (universal, @rpath Qt)
        run: |
          bundle="macos-package/obs-playlist-deck.plugin"
          mkdir -p "$bundle/Contents/MacOS" "$bundle/Contents/Resources"
          binary_src="$(find "${{ github.workspace }}/release" -type f -name 'libobs-playlist-deck.*' -print -quit)"
          data_dir="$(find "${{ github.workspace }}/release" -type d -path '*/obs-playlist-deck' -print -quit)"
          cp "$binary_src" "$bundle/Contents/MacOS/obs-playlist-deck"
          cp -R "$data_dir"/. "$bundle/Contents/Resources/"
          chmod +x "$bundle/Contents/MacOS/obs-playlist-deck"
          binary="$bundle/Contents/MacOS/obs-playlist-deck"
          cat > "$bundle/Contents/Info.plist" <<PLIST
          <?xml version="1.0" encoding="UTF-8"?>
          <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
          <plist version="1.0"><dict>
            <key>CFBundleDevelopmentRegion</key><string>en</string>
            <key>CFBundleExecutable</key><string>obs-playlist-deck</string>
            <key>CFBundleIdentifier</key><string>com.angeloruggieridj.obs-playlist-deck</string>
            <key>CFBundleName</key><string>Playlist Deck</string>
            <key>CFBundlePackageType</key><string>BNDL</string>
            <key>CFBundleShortVersionString</key><string>1.0.0</string>
            <key>CFBundleVersion</key><string>1.0.0</string>
            <key>LSMinimumSystemVersion</key><string>11.0</string>
          </dict></plist>
          PLIST
          printf 'BNDL????' > "$bundle/Contents/PkgInfo"
          otool -L "$binary" | awk 'NR>1{print $1}' | grep -E '/Qt[A-Za-z]+\.framework/' | grep -v '^@rpath/' | while read -r dep; do
            new="$(printf '%s' "$dep" | sed -E 's#^.*/(Qt[A-Za-z]+\.framework/.*)$#@rpath/\1#')"
            install_name_tool -change "$dep" "$new" "$binary"
          done
          if otool -L "$binary" | grep -E '/Qt[A-Za-z]+\.framework/' | grep -v '@rpath/'; then echo "Absolute Qt leaked"; exit 1; fi
          archs="$(lipo -archs "$binary")"; echo "archs: $archs"
          echo "$archs" | grep -q x86_64 && echo "$archs" | grep -q arm64 || { echo "not universal"; exit 1; }
          codesign --force --sign - "$binary"
          codesign --force --deep --sign - "$bundle"
          tar -czf obs-playlist-deck-macos-universal.tar.gz -C macos-package obs-playlist-deck.plugin
      - uses: actions/upload-artifact@v7
        with: { name: obs-playlist-deck-macos, path: obs-playlist-deck-macos-universal.tar.gz }

  release:
    if: startsWith(github.ref, 'refs/tags/')
    needs: [tests, linux, windows, macos]
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/download-artifact@v8
        with: { path: release-assets, merge-multiple: true }
      - uses: softprops/action-gh-release@v3
        with:
          files: |
            release-assets/obs-playlist-deck-linux-x86_64.tar.gz
            release-assets/obs-playlist-deck-windows.zip
            release-assets/obs-playlist-deck-macos-universal.tar.gz
```

- [ ] **Step 2: README.md** — short usage + install + build sections (mirror studio-player README structure, no browser content, mention macOS universal + quarantine `xattr -dr`).

- [ ] **Step 3: Commit** — `git add -A && git commit -m "ci: cross-platform build with test gate and universal macOS"`

---

### Task 10: Create public repo, push, build, verify artifacts

- [ ] **Step 1:** `gh repo create angeloruggieridj/obs-playlist-deck --public --source=. --remote=origin --push`
- [ ] **Step 2:** Watch the run; ensure `tests`, `linux`, `windows`, `macos` all green.
- [ ] **Step 3:** Download macOS artifact; verify Mach-O is universal (`x86_64 arm64`), filetype BUNDLE, Qt refs `@rpath`.
- [ ] **Step 4:** Hand the `.plugin` path to the user for on-Mac testing.

---

## Self-Review

**Spec coverage:** native dock ✔ (Tasks 6,8); existing-source dropdown binding ✔ (5,6); set path + restart + auto-advance ✔ (5,6); named playlists ✔ (6); import/export JSON+M3U ✔ (4,6); hotkeys ✔ (7); reorder + transport ✔ (6); no browser/HTTP/volume ✔ (omitted); core has no OBS/Qt ✔ (Task 1 constraint, Tasks 2-4); rigorous tests ✔ (2-4, gated in 9); Win/Linux/macOS-universal ✔ (9); public repo + build ✔ (10).

**Placeholder scan:** none — all steps carry real code/commands. (Task 1 Step 4 notes stub `.cpp` creation explicitly; Task 9 Step 2 README is described with concrete required content, acceptable as a doc task.)

**Type consistency:** `pld::PlaylistItem{path,title}`, `Playlist` methods, `pld::io::*`, `pld::mediapath::*`, `MediaSourceController` (bind/unbind/setFileAndRestart/playPause/pause/stop/restart/setOnMediaEnded/listMediaSources), `PlaylistDock` slots — names consistent across Tasks 2-8.

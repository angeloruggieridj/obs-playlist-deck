#include "doctest/doctest.h"

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::string readSource(const char* relativePath) {
    std::ifstream input(std::string(PLD_SOURCE_DIR) + "/" + relativePath, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

const std::vector<std::string>& shippedLocales() {
    static const std::vector<std::string> locales = {"de-DE", "en-US", "es-ES", "fr-FR",
                                                     "it-IT", "ja-JP", "ko-KR", "pt-BR",
                                                     "ru-RU", "zh-CN"};
    return locales;
}

// Contents of every QStringLiteral("...") in a translation unit, with escape
// sequences left exactly as the compiler will see them.
std::vector<std::string> qStringLiterals(const std::string& source) {
    const std::string marker = "QStringLiteral(\"";
    std::vector<std::string> out;
    for (auto at = source.find(marker); at != std::string::npos;
         at = source.find(marker, at + marker.size())) {
        std::string body;
        for (auto i = at + marker.size(); i < source.size(); ++i) {
            if (source[i] == '\\' && i + 1 < source.size()) {
                body += source[i];
                body += source[i + 1];
                ++i;
                continue;
            }
            if (source[i] == '"') break;
            body += source[i];
        }
        out.push_back(body);
    }
    return out;
}

bool isValidUtf8(const std::string& bytes) {
    for (size_t i = 0; i < bytes.size();) {
        const auto b = static_cast<unsigned char>(bytes[i]);
        int extra = 0;
        if (b < 0x80) extra = 0;
        else if ((b & 0xE0) == 0xC0) extra = 1;
        else if ((b & 0xF0) == 0xE0) extra = 2;
        else if ((b & 0xF8) == 0xF0) extra = 3;
        else return false;
        for (int k = 1; k <= extra; ++k) {
            if (i + k >= bytes.size()) return false;
            if ((static_cast<unsigned char>(bytes[i + k]) & 0xC0) != 0x80) return false;
        }
        i += static_cast<size_t>(extra) + 1;
    }
    return true;
}
}

// The rule the fix rests on, checked against the compiler actually building this
// release rather than taken on trust. In a UTF-16 literal a hex escape is one
// code unit, so the UTF-8 bytes of an arrow stay three separate characters; a
// universal character name is the single character meant. QStringLiteral expands
// to exactly such a literal.
static_assert(u"\u2197"[0] == 0x2197, "a UCN must yield the intended character");
static_assert(u"\u2197"[1] == 0, "and nothing after it");
static_assert(u"\xE2\x86\x97"[0] == 0x00E2, "a byte escape is a code unit, not a byte");
static_assert(u"\xE2\x86\x97"[2] == 0x0097, "which is why the arrow came out as three");

// Regression, v1.2.5: the "update available" label read
//   QStringLiteral("v%1 - <a ...>update to %3 \xE2\x86\x97</a>")
// QStringLiteral expands to a UTF-16 u"" literal, and a hex escape inside one is
// a *code unit*, not a byte: the three UTF-8 bytes of an arrow became U+00E2
// U+0086 U+0097 - an accented "a" followed by two invisible controls. Users saw
// that garbage after the version number on every platform. Non-ASCII inside a
// QStringLiteral has to be a universal character name, which the compiler
// transcodes correctly whatever the source and execution charsets are.
TEST_CASE("no QStringLiteral carries raw bytes instead of characters") {
    for (const char* file : {"src/plugin/PlaylistDock.cpp", "src/plugin/PlaylistListWidget.cpp",
                             "src/plugin/plugin-main.cpp"}) {
        const std::string source = readSource(file);
        CAPTURE(file);
        REQUIRE_FALSE(source.empty());
        for (const auto& literal : qStringLiterals(source)) {
            CAPTURE(literal);
            // A byte escape is never one character in a UTF-16 literal.
            CHECK(literal.find("\\x") == std::string::npos);
            // Raw non-ASCII depends on the compiler's source charset, which
            // MSVC gets wrong by default.
            for (char c : literal) CHECK(static_cast<unsigned char>(c) < 0x80);
        }
    }
}

// MSVC assumes the system ANSI code page for a UTF-8 source file and re-encodes
// narrow literals to it, so the Cyrillic, Chinese, Japanese and Korean language
// endonyms in the Settings dialog would ship as "?" on a Windows build.
TEST_CASE("the build pins UTF-8 as the source and execution charset on MSVC") {
    const std::string cmake = readSource("CMakeLists.txt");
    REQUIRE_FALSE(cmake.empty());
    CHECK(cmake.find("/utf-8") != std::string::npos);
    CHECK(cmake.find("if(MSVC)") != std::string::npos);
}

// OBS reads the .ini files as UTF-8. A BOM would be parsed as part of the first
// key, and any other encoding renders as mojibake in the dock.
TEST_CASE("every shipped locale is valid UTF-8 without a BOM") {
    for (const auto& loc : shippedLocales()) {
        const std::string ini = readSource(("data/locale/" + loc + ".ini").c_str());
        CAPTURE(loc);
        REQUIRE_FALSE(ini.empty());
        CHECK(ini.compare(0, 3, "\xEF\xBB\xBF") != 0);
        CHECK(isValidUtf8(ini));
    }
}

// The link text used to be a hardcoded English "update to X", inside the very
// literal that was broken. It is a translated string now.
TEST_CASE("the update link text is localized in every locale") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    REQUIRE_FALSE(dock.empty());
    CHECK(dock.find("Link.UpdateTo") != std::string::npos);
    CHECK(dock.find("update to %3") == std::string::npos);
    // The release tag comes from GitHub and lands in a rich-text label.
    CHECK(dock.find("toHtmlEscaped()") != std::string::npos);

    for (const auto& loc : shippedLocales()) {
        const std::string ini = readSource(("data/locale/" + loc + ".ini").c_str());
        CAPTURE(loc);
        const auto at = ini.find("Link.UpdateTo=\"");
        REQUIRE(at != std::string::npos);
        // It has to carry the version placeholder, or the link says nothing.
        CHECK(ini.find("%1", at) < ini.find('\n', at));
    }
}

// v1.2.5: the playlist and transport buttons show their icon only, so a full row
// fits a narrow dock. The caption moved to the tooltip; both it and the
// accessible name stay translated.
TEST_CASE("toolbar buttons are icon-only with a localized tooltip") {
    const std::string dock = readSource("src/plugin/PlaylistDock.cpp");
    REQUIRE_FALSE(dock.empty());

    // No caption reaches the button any more.
    CHECK(dock.find("QPushButton(tintedIcon(icon), \" \" + text)") == std::string::npos);
    CHECK(dock.find("QPushButton(tintedIcon(icon), QString())") != std::string::npos);
    CHECK(dock.find("setAccessibleName(name)") != std::string::npos);
    CHECK(dock.find("setToolTip(tip)") != std::string::npos);
    // A square button keeps the row compact.
    CHECK(dock.find("setFixedSize(kButtonPx, kButtonPx)") != std::string::npos);

    // Both halves of every button's text stay translated: the Btn.* key feeds
    // the accessible name, the Tip.* key the tooltip.
    for (const char* key : {"Refresh", "Add", "Remove", "Up", "Down", "Clear", "Play", "Prev",
                            "Pause", "Stop", "Next", "Save", "Open", "Settings"}) {
        CAPTURE(key);
        CHECK(dock.find(std::string("T(\"Btn.") + key + "\")") != std::string::npos);
        CHECK(dock.find(std::string("T(\"Tip.") + key + "\")") != std::string::npos);
        for (const auto& loc : shippedLocales()) {
            const std::string ini = readSource(("data/locale/" + loc + ".ini").c_str());
            CAPTURE(loc);
            CHECK(ini.find(std::string("Btn.") + key + "=\"") != std::string::npos);
            CHECK(ini.find(std::string("Tip.") + key + "=\"") != std::string::npos);
        }
    }
}

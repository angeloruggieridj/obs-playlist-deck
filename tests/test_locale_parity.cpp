#include "doctest/doctest.h"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The locale files are generated from tools/locales.json, and CI runs
// `gen_locales.py --check` as well. These cases keep the same guarantees inside
// the unit-test suite, where a contributor sees them without running Python.
//
// The failure they exist for: a new string added to en-US and forgotten in the
// other nine shipped languages. Nothing used to notice — the tests only checked
// that specific keys existed for specific features, so a key missing from six
// locales shipped unremarked.
namespace {
std::string readSource(const std::string& relativePath) {
    std::ifstream input(std::string(PLD_SOURCE_DIR) + "/" + relativePath, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

const std::vector<std::string>& shippedLocales() {
    static const std::vector<std::string> locales = {"de-DE", "en-US", "es-ES", "fr-FR",
                                                     "it-IT", "ja-JP", "ko-KR", "pt-BR",
                                                     "ru-RU", "zh-CN"};
    return locales;
}

struct Entry {
    std::string key;
    std::string value;
};

std::vector<Entry> parseIni(const std::string& text) {
    std::vector<Entry> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq + 2 >= line.size()) continue;
        if (line[eq + 1] != '"' || line.back() != '"') continue;
        out.push_back({line.substr(0, eq), line.substr(eq + 2, line.size() - eq - 3)});
    }
    return out;
}

// Placeholders a translation has to carry over, or the string says less than
// the English one does ("Playing: %1" without the %1 names nothing).
std::set<std::string> placeholders(const std::string& text) {
    std::set<std::string> out;
    for (size_t i = 0; i + 1 < text.size(); ++i)
        if (text[i] == '%' && std::isdigit(static_cast<unsigned char>(text[i + 1])))
            out.insert(text.substr(i, 2));
    return out;
}
} // namespace

TEST_CASE("every locale defines exactly the same keys as en-US") {
    const auto english = parseIni(readSource("data/locale/en-US.ini"));
    REQUIRE_FALSE(english.empty());
    std::set<std::string> englishKeys;
    for (const auto& e : english) englishKeys.insert(e.key);

    for (const auto& loc : shippedLocales()) {
        const auto entries = parseIni(readSource("data/locale/" + loc + ".ini"));
        CAPTURE(loc);
        REQUIRE_FALSE(entries.empty());
        std::set<std::string> keys;
        for (const auto& e : entries) keys.insert(e.key);

        for (const auto& key : englishKeys) {
            CAPTURE(key);
            CHECK(keys.count(key) == 1);
        }
        for (const auto& key : keys) {
            CAPTURE(key);
            CHECK(englishKeys.count(key) == 1); // no leftovers either
        }
    }
}

TEST_CASE("every translation keeps the placeholders of its English text") {
    const auto english = parseIni(readSource("data/locale/en-US.ini"));
    for (const auto& loc : shippedLocales()) {
        const auto entries = parseIni(readSource("data/locale/" + loc + ".ini"));
        for (const auto& e : entries) {
            const auto match = std::find_if(english.begin(), english.end(),
                                            [&](const Entry& x) { return x.key == e.key; });
            if (match == english.end()) continue;
            CAPTURE(loc);
            CAPTURE(e.key);
            CHECK(placeholders(e.value) == placeholders(match->value));
        }
    }
}

TEST_CASE("no translated string is left empty") {
    for (const auto& loc : shippedLocales()) {
        for (const auto& e : parseIni(readSource("data/locale/" + loc + ".ini"))) {
            CAPTURE(loc);
            CAPTURE(e.key);
            CHECK_FALSE(e.value.empty());
        }
    }
}

// F-14: the generator used to carry its own copy of the key list, four releases
// out of date. Running it would have deleted every string added since.
TEST_CASE("the locale generator reads the same table the files are built from") {
    const std::string script = readSource("tools/gen_locales.py");
    REQUIRE_FALSE(script.empty());
    CHECK(script.find("locales.json") != std::string::npos);
    CHECK(script.find("--check") != std::string::npos);
    // A hardcoded key list in the script is exactly the bug this replaced.
    CHECK(script.find("KEYS = [") == std::string::npos);
    CHECK_FALSE(readSource("tools/locales.json").empty());
}

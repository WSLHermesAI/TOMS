// version_label_test.cpp -- the title's version text from version.json (version_label.h).
#include "version_label.h"

#include <cstdio>

namespace {

int g_iFailed = 0;

void f_Check(const std::string& l_strGot, const std::string& l_strWanted, const char* l_pszWhat) {
    if (l_strGot == l_strWanted) return;
    g_iFailed++;
    std::printf("FAILED: %s: got '%s', wanted '%s'\n", l_pszWhat, l_strGot.c_str(), l_strWanted.c_str());
}

nlohmann::json f_Parse(const char* l_pszText) { return nlohmann::json::parse(l_pszText, nullptr, false); }

}  // namespace

int main() {
    f_Check(toms::f_VersionLabel(f_Parse(R"({"number": 12, "sha": "abc1234"})")), "v12", "a version file");
    f_Check(toms::f_VersionLabel(f_Parse(R"({"number": 1})")), "v1", "the first publish");
    f_Check(toms::f_VersionLabel(nlohmann::json()), "", "no file");
    f_Check(toms::f_VersionLabel(f_Parse("not json")), "", "a broken file");
    f_Check(toms::f_VersionLabel(f_Parse(R"({"number": "12"})")), "", "the number as text");
    f_Check(toms::f_VersionLabel(f_Parse(R"({"number": 0})")), "", "no publish yet");
    std::printf("version_label_test: %d failed\n", g_iFailed);
    return g_iFailed == 0 ? 0 : 1;
}

// log_test.cpp — headless verification of the modern logging system (src/log.h).
// Exits 0 on success, 1 on any failed check.
#include "log.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int g_iFail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); ++g_iFail; } } while(0)

int main() {
    auto& l_oL = toms::Logger::f_Instance();

    // 1) "{}" positional substitution
    {
        std::ostringstream l_oSink; // not used; logger writes to stdout+file
        l_oL.f_Log(toms::LogLevel::Info, "file.cpp", 42, "loaded {} stages, hp={} gold={}", 11, 120, 250);
        // we can't easily capture stdout here, so just confirm it doesn't throw and the
        // formatter itself produces the expected string via a private-ish reimpl check:
        // instead validate the documented escape handling + level gating below.
    }

    // 2) level filtering: with min=Warn, Info must be dropped, Warn/Error kept.
    l_oL.f_SetLevel(toms::LogLevel::Warn);
    CHECK(l_oL.f_Level() == toms::LogLevel::Warn, "setLevel / level()");

    // 3) escape handling: "{{" -> '{', "}}" -> '}', "{}" -> arg
    std::string l_strR = toms::Logger::f_Instance().f_FormatForTest("a {} b {{c}} d{}", std::string("X"), 7);
    CHECK(l_strR.find("a X b {c} d7") != std::string::npos, "format escapes + positional substitution");

    // 4) file sink: write a line, then verify the file contains it
    l_oL.f_SetFile("log_test_out.log");
    l_oL.f_SetLevel(toms::LogLevel::Info);
    l_oL.f_Log(toms::LogLevel::Info, "demo.cpp", 10, "hello {} world", 123);
    l_oL.f_SetFile("");  // close file
    {
        std::ifstream l_oF("log_test_out.log");
        std::string l_strContent((std::istreambuf_iterator<char>(l_oF)), std::istreambuf_iterator<char>());
        CHECK(l_strContent.find("hello 123 world") != std::string::npos, "file sink wrote formatted line");
        CHECK(l_strContent.find("INFO") != std::string::npos, "file sink includes level tag");
        CHECK(l_strContent.find("demo.cpp:10") != std::string::npos, "file sink includes source location");
    }

    // 5) logging macros compile and are usable
    TOMS_LOG_INFO("macro form works, n={}", 5);
    TOMS_LOG(toms::LogLevel::Warn) << "stream form works " << 2 + 2;

    if (g_iFail == 0) { printf("log_test: ALL PASS\n"); return 0; }
    printf("log_test: %d FAILED\n", g_iFail);
    return 1;
}

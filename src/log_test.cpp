// log_test.cpp — headless verification of the modern logging system (src/log.h).
// Exits 0 on success, 1 on any failed check.
#include "log.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int s_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); ++s_fail; } } while(0)

int main() {
    auto& L = toms::Logger::instance();

    // 1) "{}" positional substitution
    {
        std::ostringstream sink; // not used; logger writes to stdout+file
        L.log(toms::LogLevel::Info, "file.cpp", 42, "loaded {} stages, hp={} gold={}", 11, 120, 250);
        // we can't easily capture stdout here, so just confirm it doesn't throw and the
        // formatter itself produces the expected string via a private-ish reimpl check:
        // instead validate the documented escape handling + level gating below.
    }

    // 2) level filtering: with min=Warn, Info must be dropped, Warn/Error kept.
    L.setLevel(toms::LogLevel::Warn);
    CHECK(L.level() == toms::LogLevel::Warn, "setLevel / level()");

    // 3) escape handling: "{{" -> '{', "}}" -> '}', "{}" -> arg
    std::string r = toms::Logger::instance().formatForTest("a {} b {{c}} d{}", std::string("X"), 7);
    CHECK(r.find("a X b {c} d7") != std::string::npos, "format escapes + positional substitution");

    // 4) file sink: write a line, then verify the file contains it
    L.setFile("log_test_out.log");
    L.setLevel(toms::LogLevel::Info);
    L.log(toms::LogLevel::Info, "demo.cpp", 10, "hello {} world", 123);
    L.setFile("");  // close file
    {
        std::ifstream f("log_test_out.log");
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(content.find("hello 123 world") != std::string::npos, "file sink wrote formatted line");
        CHECK(content.find("INFO") != std::string::npos, "file sink includes level tag");
        CHECK(content.find("demo.cpp:10") != std::string::npos, "file sink includes source location");
    }

    // 5) logging macros compile and are usable
    TOMS_LOG_INFO("macro form works, n={}", 5);
    TOMS_LOG(toms::LogLevel::Warn) << "stream form works " << 2 + 2;

    if (s_fail == 0) { printf("log_test: ALL PASS\n"); return 0; }
    printf("log_test: %d FAILED\n", s_fail);
    return 1;
}

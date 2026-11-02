// log.h — modern, dependency-free logging for TOMS (C++17).
//
// Mixes cleanly with the FMLog-style lifecycle reporting: Object::DumpLeaks() and
// the batch report now go through this Logger instead of raw std::cout.
//
// Features (all standard-library only):
//   * level filtering (Trace < Debug < Info < Warn < Error < Fatal)
//   * "{}" positional substitution via a tiny fold-expression formatter
//       TOMS_LOG_INFO("loaded {} stages, hp={}", n, hp);
//   * streaming form:  TOMS_LOG(LogLevel::Warn) << "hp=" << hp;
//   * source location (file:line) + timestamp on every line
//   * thread-safe (one global mutex around emit)
//   * optional file sink via Logger::instance().setFile("run.log")
//
// No external dependencies (no spdlog/fmt) so it builds no-root on WSL.
#pragma once
#include <string>
#include <string_view>
#include <sstream>
#include <iostream>
#if defined(__ANDROID__)
#  include <android/log.h>     // __android_log_print: the only place adb logcat can see
#endif
#include <mutex>
#include <chrono>
#include <iomanip>
#include <vector>
#include <fstream>
#include <cstring>

namespace toms {

enum class LogLevel : int { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Fatal = 5 };

constexpr const char* f_LogLevelName(LogLevel l_eL) {
    switch (l_eL) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "?";
}

class Logger {
public:
    static Logger& f_Instance() { static Logger l_oL; return l_oL; }

    void f_SetLevel(LogLevel l_eL) { m_eMinLevel = l_eL; }
    LogLevel f_Level() const { return m_eMinLevel; }

    // empty path disables the file sink
    void f_SetFile(const std::string& l_strPath) {
        std::lock_guard<std::mutex> l_oLk(m_oMutex);
        m_oFile.close();
        if (!l_strPath.empty()) {
            m_oFile.open(l_strPath, std::ios::out | std::ios::app);
        }
    }

    // ---- "{}" format API ----
    template <class... Args>
    void f_Log(LogLevel l_eLvl, const char* l_pszFile, int l_iLine, std::string_view l_svFmt, Args&&... l_oArgs) {
        if (l_eLvl < m_eMinLevel) return;
        std::string l_strMsg = f_FormatImpl(l_svFmt, std::forward<Args>(l_oArgs)...);
        f_Emit(l_eLvl, l_pszFile, l_iLine, l_strMsg);
    }

    // ---- streaming API:  TOMS_LOG(LogLevel::Warn) << "hp=" << hp; ----
    struct Stream {
        LogLevel m_eLvl; const char* m_pszFile; int m_iLine; bool m_bEnabled; std::ostringstream m_oOss;
        ~Stream() { if (m_bEnabled) Logger::f_Instance().f_Emit(m_eLvl, m_pszFile, m_iLine, m_oOss.str()); }
        template <class T> Stream& operator<<(T&& l_oV) { m_oOss << std::forward<T>(l_oV); return *this; }
    };

private:
    Logger() = default;
    ~Logger() { if (m_oFile.is_open()) m_oFile.close(); }

    // stringify one argument (numbers / strings / string_view / const char*)
    template <class T>
    static std::string f_ToStr(T&& l_oV) {
        using U = std::decay_t<T>;
        if constexpr (std::is_same_v<U, std::string>)
            return std::forward<T>(l_oV);
        else if constexpr (std::is_convertible_v<U, std::string_view>)
            return std::string(std::forward<T>(l_oV));
        else {
            std::ostringstream l_oOs;
            l_oOs << std::forward<T>(l_oV);
            return l_oOs.str();
        }
    }

    // build the message: replace "{}" with args (supports "{{" / "}}" escapes)
    template <class... Args>
    static std::string f_FormatImpl(std::string_view l_svFmt, Args&&... l_oArgs) {
        std::vector<std::string> l_vStrs{ f_ToStr(std::forward<Args>(l_oArgs))... };
        std::string l_strOut; l_strOut.reserve(l_svFmt.size() + 16);
        size_t l_nArgN = 0;
        for (size_t l_nI = 0; l_nI < l_svFmt.size(); ) {
            if (l_nI + 1 < l_svFmt.size() && l_svFmt[l_nI] == '{' && l_svFmt[l_nI + 1] == '}') {
                if (l_nArgN < l_vStrs.size()) { l_strOut += l_vStrs[l_nArgN++]; l_nI += 2; }
                else { l_strOut += "{}"; l_nI += 2; }
            } else if (l_nI + 1 < l_svFmt.size() && l_svFmt[l_nI] == '{' && l_svFmt[l_nI + 1] == '{') {
                l_strOut += '{'; l_nI += 2;
            } else if (l_nI + 1 < l_svFmt.size() && l_svFmt[l_nI] == '}' && l_svFmt[l_nI + 1] == '}') {
                l_strOut += '}'; l_nI += 2;
            } else { l_strOut += l_svFmt[l_nI]; ++l_nI; }
        }
        return l_strOut;
    }

    void f_Emit(LogLevel l_eLvl, const char* l_pszFile, int l_iLine, const std::string& l_strMsg) {
        std::lock_guard<std::mutex> l_oLk(m_oMutex);
        // timestamp
        auto l_oNow = std::chrono::system_clock::now();
        auto l_i64T = std::chrono::system_clock::to_time_t(l_oNow);
        std::tm l_oTm{};
#if defined(_WIN32)
        localtime_s(&l_oTm, &l_i64T);
#else
        localtime_r(&l_i64T, &l_oTm);
#endif
        char l_pszTs[16];
        std::strftime(l_pszTs, sizeof(l_pszTs), "%H:%M:%S", &l_oTm);
        // strip path to basename
        std::string l_strFloc = l_pszFile ? l_pszFile : "";
        size_t l_nSlash = l_strFloc.find_last_of("/\\");
        if (l_nSlash != std::string::npos) l_strFloc = l_strFloc.substr(l_nSlash + 1);
        std::string l_strLineStr =
            std::string("[") + l_pszTs + "] " + f_LogLevelName(l_eLvl) + " " +
            l_strFloc + ":" + std::to_string(l_iLine) + "  " + l_strMsg + "\n";
        std::cout << l_strLineStr;
#if defined(__ANDROID__)
        {   // On a device stdout goes nowhere visible: Android's log is read with adb logcat. A copy
            // without the trailing newline, because logcat appends its own per entry -- lineStr itself
            // is left untouched so the stream and the file sink still see exactly the same text.
            static const char* l_pszTag = "toms";
            const int l_iPrio = l_eLvl == LogLevel::Fatal ? ANDROID_LOG_FATAL
                           : l_eLvl == LogLevel::Error ? ANDROID_LOG_ERROR
                           : l_eLvl == LogLevel::Warn  ? ANDROID_LOG_WARN
                           : l_eLvl == LogLevel::Info  ? ANDROID_LOG_INFO
                                                    : ANDROID_LOG_DEBUG;
            if (!l_strLineStr.empty() && l_strLineStr.back() == '\n') __android_log_print(l_iPrio, l_pszTag, "%s", l_strLineStr.substr(0, l_strLineStr.size() - 1).c_str());
            else                                             __android_log_print(l_iPrio, l_pszTag, "%s", l_strLineStr.c_str());
        }
#endif
        if (m_oFile.is_open()) m_oFile << l_strLineStr;
    }

    LogLevel        m_eMinLevel = LogLevel::Info;
    std::mutex      m_oMutex;
    std::ofstream   m_oFile;

public:
    // test helper: expose the "{}" formatter (used by log_test.cpp)
    template <class... Args>
    std::string f_FormatForTest(std::string_view l_svFmt, Args&&... l_oArgs) {
        return f_FormatImpl(l_svFmt, std::forward<Args>(l_oArgs)...);
    }
};

} // namespace toms

// ---- convenience macros (FTM: file/line injected automatically) ----
#define TOMS_LOG(lvl) \
    toms::Logger::Stream{ (lvl), __FILE__, __LINE__, \
        (toms::Logger::f_Instance().f_Level() <= (lvl)), std::ostringstream{} }
#define TOMS_LOG_TRACE(...) toms::Logger::instance().log(toms::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)
#define TOMS_LOG_DEBUG(...) toms::Logger::instance().log(toms::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define TOMS_LOG_INFO(...)  toms::Logger::f_Instance().f_Log(toms::LogLevel::Info,  __FILE__, __LINE__, __VA_ARGS__)
#define TOMS_LOG_WARN(...)  toms::Logger::instance().log(toms::LogLevel::Warn,  __FILE__, __LINE__, __VA_ARGS__)
#define TOMS_LOG_ERROR(...) toms::Logger::f_Instance().f_Log(toms::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
#define TOMS_LOG_FATAL(...) toms::Logger::instance().log(toms::LogLevel::Fatal, __FILE__, __LINE__, __VA_ARGS__)

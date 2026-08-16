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
#include <mutex>
#include <chrono>
#include <iomanip>
#include <vector>
#include <fstream>
#include <cstring>

namespace toms {

enum class LogLevel : int { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Fatal = 5 };

constexpr const char* logLevelName(LogLevel l) {
    switch (l) {
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
    static Logger& instance() { static Logger l; return l; }

    void setLevel(LogLevel l) { minLevel_ = l; }
    LogLevel level() const { return minLevel_; }

    // empty path disables the file sink
    void setFile(const std::string& path) {
        std::lock_guard<std::mutex> lk(mutex_);
        file_.close();
        if (!path.empty()) {
            file_.open(path, std::ios::out | std::ios::app);
        }
    }

    // ---- "{}" format API ----
    template <class... Args>
    void log(LogLevel lvl, const char* file, int line, std::string_view fmt, Args&&... args) {
        if (lvl < minLevel_) return;
        std::string msg = formatImpl(fmt, std::forward<Args>(args)...);
        emit(lvl, file, line, msg);
    }

    // ---- streaming API:  TOMS_LOG(LogLevel::Warn) << "hp=" << hp; ----
    struct Stream {
        LogLevel lvl; const char* file; int line; bool enabled; std::ostringstream oss;
        ~Stream() { if (enabled) Logger::instance().emit(lvl, file, line, oss.str()); }
        template <class T> Stream& operator<<(T&& v) { oss << std::forward<T>(v); return *this; }
    };

private:
    Logger() = default;
    ~Logger() { if (file_.is_open()) file_.close(); }

    // stringify one argument (numbers / strings / string_view / const char*)
    template <class T>
    static std::string toStr(T&& v) {
        using U = std::decay_t<T>;
        if constexpr (std::is_same_v<U, std::string>)
            return std::forward<T>(v);
        else if constexpr (std::is_convertible_v<U, std::string_view>)
            return std::string(std::forward<T>(v));
        else {
            std::ostringstream os;
            os << std::forward<T>(v);
            return os.str();
        }
    }

    // build the message: replace "{}" with args (supports "{{" / "}}" escapes)
    template <class... Args>
    static std::string formatImpl(std::string_view fmt, Args&&... args) {
        std::vector<std::string> strs{ toStr(std::forward<Args>(args))... };
        std::string out; out.reserve(fmt.size() + 16);
        size_t argN = 0;
        for (size_t i = 0; i < fmt.size(); ) {
            if (i + 1 < fmt.size() && fmt[i] == '{' && fmt[i + 1] == '}') {
                if (argN < strs.size()) { out += strs[argN++]; i += 2; }
                else { out += "{}"; i += 2; }
            } else if (i + 1 < fmt.size() && fmt[i] == '{' && fmt[i + 1] == '{') {
                out += '{'; i += 2;
            } else if (i + 1 < fmt.size() && fmt[i] == '}' && fmt[i + 1] == '}') {
                out += '}'; i += 2;
            } else { out += fmt[i]; ++i; }
        }
        return out;
    }

    void emit(LogLevel lvl, const char* file, int line, const std::string& msg) {
        std::lock_guard<std::mutex> lk(mutex_);
        // timestamp
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char ts[16];
        std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm);
        // strip path to basename
        std::string floc = file ? file : "";
        size_t slash = floc.find_last_of("/\\");
        if (slash != std::string::npos) floc = floc.substr(slash + 1);
        std::string lineStr =
            std::string("[") + ts + "] " + logLevelName(lvl) + " " +
            floc + ":" + std::to_string(line) + "  " + msg + "\n";
        std::cout << lineStr;
        if (file_.is_open()) file_ << lineStr;
    }

    LogLevel        minLevel_ = LogLevel::Info;
    std::mutex      mutex_;
    std::ofstream   file_;

public:
    // test helper: expose the "{}" formatter (used by log_test.cpp)
    template <class... Args>
    std::string formatForTest(std::string_view fmt, Args&&... args) {
        return formatImpl(fmt, std::forward<Args>(args)...);
    }
};

} // namespace toms

// ---- convenience macros (FTM: file/line injected automatically) ----
#define TOMS_LOG(lvl) \
    toms::Logger::Stream{ (lvl), __FILE__, __LINE__, \
        (toms::Logger::instance().level() <= (lvl)), std::ostringstream{} }
#define TOMS_LOG_TRACE(...) toms::Logger::instance().log(toms::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)
#define TOMS_LOG_DEBUG(...) toms::Logger::instance().log(toms::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define TOMS_LOG_INFO(...)  toms::Logger::instance().log(toms::LogLevel::Info,  __FILE__, __LINE__, __VA_ARGS__)
#define TOMS_LOG_WARN(...)  toms::Logger::instance().log(toms::LogLevel::Warn,  __FILE__, __LINE__, __VA_ARGS__)
#define TOMS_LOG_ERROR(...) toms::Logger::instance().log(toms::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
#define TOMS_LOG_FATAL(...) toms::Logger::instance().log(toms::LogLevel::Fatal, __FILE__, __LINE__, __VA_ARGS__)

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace projectunity::core {

enum class LogLevel : std::uint8_t {
    Trace,
    Info,
    Warning,
    Error,
    Fatal,
    Security,
    Network
};

enum class LogCategory : std::uint8_t {
    Core,
    Renderer,
    Editor,
    Assets,
    Terrain,
    Physics,
    Navigation,
    UI,
    Network,
    Security,
    Server,
    Client
};

struct LogEntry {
    std::chrono::system_clock::time_point timestamp {};
    std::thread::id threadId {};
    LogLevel level {LogLevel::Info};
    LogCategory category {LogCategory::Core};
    std::string message;
    std::string file;
    std::uint_least32_t line {0};
};

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(const LogEntry& entry) = 0;
};

class Logger final {
public:
    static Logger& instance();

    void addSink(std::shared_ptr<ILogSink> sink);
    void removeSink(const ILogSink* sink);
    void clearSinks();

    void setMinimumLevel(LogLevel level);
    [[nodiscard]] LogLevel minimumLevel() const;

    void log(
        LogLevel level,
        LogCategory category,
        std::string_view message,
        const std::source_location& location = std::source_location::current());

private:
    Logger() = default;

    mutable std::mutex mutex_;
    LogLevel minimumLevel_ {LogLevel::Trace};
    std::vector<std::shared_ptr<ILogSink>> sinks_;
};

class ConsoleLogSink final : public ILogSink {
public:
    void write(const LogEntry& entry) override;
};

class MemoryLogSink final : public ILogSink {
public:
    void write(const LogEntry& entry) override;
    [[nodiscard]] std::vector<LogEntry> entries() const;
    [[nodiscard]] std::vector<LogEntry> drain();

private:
    mutable std::mutex mutex_;
    std::vector<LogEntry> entries_;
};

[[nodiscard]] std::string_view toString(LogLevel level);
[[nodiscard]] std::string_view toString(LogCategory category);
[[nodiscard]] std::string redactSensitive(std::string_view message);

void logInfo(
    LogCategory category,
    std::string_view message,
    const std::source_location& location = std::source_location::current());

void logWarning(
    LogCategory category,
    std::string_view message,
    const std::source_location& location = std::source_location::current());

void logError(
    LogCategory category,
    std::string_view message,
    const std::source_location& location = std::source_location::current());

void logSecurity(
    LogCategory category,
    std::string_view message,
    const std::source_location& location = std::source_location::current());

} // namespace projectunity::core

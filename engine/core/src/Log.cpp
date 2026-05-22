#include <projectunity/core/Log.hpp>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace projectunity::core {
namespace {

[[nodiscard]] bool shouldLog(LogLevel messageLevel, LogLevel minimumLevel)
{
    return static_cast<std::uint8_t>(messageLevel) >= static_cast<std::uint8_t>(minimumLevel);
}

[[nodiscard]] std::string lowerCopy(std::string_view text)
{
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lower;
}

[[nodiscard]] bool isSensitiveTerminator(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == ';' || ch == '&' || ch == ',';
}

void redactValueAfterKey(std::string& output, std::string_view key)
{
    std::size_t searchOffset = 0;
    while (true) {
        const auto lower = lowerCopy(output);
        const auto found = lower.find(key, searchOffset);
        if (found == std::string::npos) {
            return;
        }

        const auto valueBegin = found + key.size();
        auto valueEnd = valueBegin;
        while (valueEnd < output.size() && !isSensitiveTerminator(output[valueEnd])) {
            ++valueEnd;
        }

        if (valueBegin < valueEnd) {
            output.replace(valueBegin, valueEnd - valueBegin, "<redacted>");
            searchOffset = valueBegin + std::string_view("<redacted>").size();
        } else {
            searchOffset = valueBegin;
        }
    }
}

[[nodiscard]] std::string formatEntry(const LogEntry& entry)
{
    const auto time = std::chrono::system_clock::to_time_t(entry.timestamp);
    std::tm localTime {};

#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%H:%M:%S")
           << " [" << toString(entry.level) << "]"
           << " [" << toString(entry.category) << "] "
           << entry.message
           << " (" << entry.file << ":" << entry.line << ")";
    return stream.str();
}

} // namespace

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::addSink(std::shared_ptr<ILogSink> sink)
{
    if (!sink) {
        return;
    }

    std::lock_guard lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::removeSink(const ILogSink* sink)
{
    std::lock_guard lock(mutex_);
    sinks_.erase(
        std::remove_if(sinks_.begin(), sinks_.end(), [sink](const std::shared_ptr<ILogSink>& current) {
            return current.get() == sink;
        }),
        sinks_.end());
}

void Logger::clearSinks()
{
    std::lock_guard lock(mutex_);
    sinks_.clear();
}

void Logger::setMinimumLevel(LogLevel level)
{
    std::lock_guard lock(mutex_);
    minimumLevel_ = level;
}

LogLevel Logger::minimumLevel() const
{
    std::lock_guard lock(mutex_);
    return minimumLevel_;
}

void Logger::log(
    LogLevel level,
    LogCategory category,
    std::string_view message,
    const std::source_location& location)
{
    std::vector<std::shared_ptr<ILogSink>> sinks;
    {
        std::lock_guard lock(mutex_);
        if (!shouldLog(level, minimumLevel_)) {
            return;
        }
        sinks = sinks_;
    }

    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.threadId = std::this_thread::get_id();
    entry.level = level;
    entry.category = category;
    entry.message = redactSensitive(message);
    entry.file = location.file_name();
    entry.line = location.line();

    for (const auto& sink : sinks) {
        if (sink) {
            sink->write(entry);
        }
    }
}

void ConsoleLogSink::write(const LogEntry& entry)
{
    auto& stream = entry.level == LogLevel::Error || entry.level == LogLevel::Fatal ? std::cerr : std::cout;
    stream << formatEntry(entry) << '\n';
}

void MemoryLogSink::write(const LogEntry& entry)
{
    std::lock_guard lock(mutex_);
    entries_.push_back(entry);
}

std::vector<LogEntry> MemoryLogSink::entries() const
{
    std::lock_guard lock(mutex_);
    return entries_;
}

std::vector<LogEntry> MemoryLogSink::drain()
{
    std::lock_guard lock(mutex_);
    std::vector<LogEntry> drained;
    drained.swap(entries_);
    return drained;
}

std::string_view toString(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return "Trace";
    case LogLevel::Info:
        return "Info";
    case LogLevel::Warning:
        return "Warning";
    case LogLevel::Error:
        return "Error";
    case LogLevel::Fatal:
        return "Fatal";
    case LogLevel::Security:
        return "Security";
    case LogLevel::Network:
        return "Network";
    }
    return "Unknown";
}

std::string_view toString(LogCategory category)
{
    switch (category) {
    case LogCategory::Core:
        return "Core";
    case LogCategory::Renderer:
        return "Renderer";
    case LogCategory::Editor:
        return "Editor";
    case LogCategory::Assets:
        return "Assets";
    case LogCategory::Terrain:
        return "Terrain";
    case LogCategory::Physics:
        return "Physics";
    case LogCategory::Navigation:
        return "Navigation";
    case LogCategory::UI:
        return "UI";
    case LogCategory::Network:
        return "Network";
    case LogCategory::Security:
        return "Security";
    case LogCategory::Server:
        return "Server";
    case LogCategory::Client:
        return "Client";
    }
    return "Unknown";
}

std::string redactSensitive(std::string_view message)
{
    std::string output(message);

    redactValueAfterKey(output, "token=");
    redactValueAfterKey(output, "session=");
    redactValueAfterKey(output, "password=");
    redactValueAfterKey(output, "secret=");
    redactValueAfterKey(output, "authorization: bearer ");

    return output;
}

void logInfo(LogCategory category, std::string_view message, const std::source_location& location)
{
    Logger::instance().log(LogLevel::Info, category, message, location);
}

void logWarning(LogCategory category, std::string_view message, const std::source_location& location)
{
    Logger::instance().log(LogLevel::Warning, category, message, location);
}

void logError(LogCategory category, std::string_view message, const std::source_location& location)
{
    Logger::instance().log(LogLevel::Error, category, message, location);
}

void logSecurity(LogCategory category, std::string_view message, const std::source_location& location)
{
    Logger::instance().log(LogLevel::Security, category, message, location);
}

} // namespace projectunity::core

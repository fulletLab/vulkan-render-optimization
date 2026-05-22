#include <projectunity/core/Log.hpp>
#include <projectunity/core/StableId.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int fail(const char* message)
{
    std::cerr << message << '\n';
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    using namespace projectunity::core;

    Logger::instance().clearSinks();
    auto sink = std::make_shared<MemoryLogSink>();
    Logger::instance().addSink(sink);
    Logger::instance().setMinimumLevel(LogLevel::Trace);

    logInfo(LogCategory::Core, "core test token=abc123 password=secret");
    const auto entries = sink->entries();
    if (entries.size() != 1) {
        return fail("expected one log entry");
    }

    if (entries.front().message.find("abc123") != std::string::npos) {
        return fail("token was not redacted");
    }

    if (entries.front().message.find("secret") != std::string::npos) {
        return fail("password was not redacted");
    }

    StableIdGenerator generator;
    const auto first = generator.next();
    const auto second = generator.next();
    if (!first.isValid() || !second.isValid() || first == second) {
        return fail("stable id generator returned invalid ids");
    }

    Logger::instance().clearSinks();
    return EXIT_SUCCESS;
}

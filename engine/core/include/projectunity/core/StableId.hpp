#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace projectunity::core {

class StableId final {
public:
    constexpr StableId() = default;
    explicit constexpr StableId(std::uint64_t value) noexcept
        : value_(value)
    {
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept
    {
        return value_;
    }

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return value_ != 0;
    }

    friend constexpr bool operator==(StableId lhs, StableId rhs) noexcept
    {
        return lhs.value_ == rhs.value_;
    }

    friend constexpr bool operator!=(StableId lhs, StableId rhs) noexcept
    {
        return !(lhs == rhs);
    }

private:
    std::uint64_t value_ {0};
};

class StableIdGenerator final {
public:
    explicit StableIdGenerator(std::uint64_t firstValue = 1) noexcept
        : next_(firstValue == 0 ? 1 : firstValue)
    {
    }

    [[nodiscard]] StableId next()
    {
        const auto value = next_.fetch_add(1, std::memory_order_relaxed);
        if (value == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("StableIdGenerator exhausted its id space");
        }
        return StableId(value);
    }

    void reset(std::uint64_t nextValue = 1) noexcept
    {
        next_.store(nextValue == 0 ? 1 : nextValue, std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> next_;
};

} // namespace projectunity::core

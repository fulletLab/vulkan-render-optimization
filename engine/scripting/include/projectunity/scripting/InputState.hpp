#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace projectunity::scripting {

enum class KeyCode : std::uint8_t {
    W,
    A,
    S,
    D,
    Q,
    E,
    Space,
    LeftShift,
    Count,
};

struct InputState {
    std::array<bool, static_cast<std::size_t>(KeyCode::Count)> keys {};
    float mouseDeltaX {0.0F};
    float mouseDeltaY {0.0F};
    bool mouseLook {false};

    [[nodiscard]] bool keyDown(KeyCode key) const noexcept
    {
        return keys[static_cast<std::size_t>(key)];
    }

    void setKeyDown(KeyCode key, bool down) noexcept
    {
        keys[static_cast<std::size_t>(key)] = down;
    }

    void clearFrameDeltas() noexcept
    {
        mouseDeltaX = 0.0F;
        mouseDeltaY = 0.0F;
    }
};

} // namespace projectunity::scripting

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
    Tab,
    Escape,
    Count,
};

class InputService {
public:
    virtual ~InputService() = default;
    virtual void setMouseCaptured(bool captured) = 0;
    [[nodiscard]] virtual bool isMouseCaptured() const noexcept = 0;
};

struct InputState {
    std::array<bool, static_cast<std::size_t>(KeyCode::Count)> keys {};
    std::array<bool, static_cast<std::size_t>(KeyCode::Count)> pressedKeys {};
    float mouseDeltaX {0.0F};
    float mouseDeltaY {0.0F};
    bool mouseCaptured {false};

    [[nodiscard]] bool keyDown(KeyCode key) const noexcept
    {
        return keys[static_cast<std::size_t>(key)];
    }

    [[nodiscard]] bool keyPressed(KeyCode key) const noexcept
    {
        return pressedKeys[static_cast<std::size_t>(key)];
    }

    void setKeyDown(KeyCode key, bool down) noexcept
    {
        const auto index = static_cast<std::size_t>(key);
        if (down && !keys[index]) {
            pressedKeys[index] = true;
        }
        keys[index] = down;
    }

    void clearFrameDeltas() noexcept
    {
        pressedKeys.fill(false);
        mouseDeltaX = 0.0F;
        mouseDeltaY = 0.0F;
    }
};

} // namespace projectunity::scripting

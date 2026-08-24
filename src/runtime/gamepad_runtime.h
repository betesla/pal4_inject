#pragma once

#include "pal4inject/gamepad.h"

namespace pal4::inject {

struct GamepadModernControlState {
    GamepadAnalogStick movement{};
    GamepadAnalogStick camera{};
    bool active = false;
};

void TickGamepadInput();
GamepadModernControlState GetGamepadModernControlState() noexcept;
void NotifyMouseInputActivity() noexcept;
void ReassertGamepadCursorHidden() noexcept;
bool ShouldSuppressNativeCursor() noexcept;

}  // namespace pal4::inject

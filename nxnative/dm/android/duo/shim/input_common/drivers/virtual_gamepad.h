// nx2android: Eden's virtual gamepad as the companion's own button source in the port's HID (hle_hid.c dm_duo_pad, ORed with the player's)
#pragma once
#include <cstddef>
#include "common/common_types.h"
namespace InputCommon {
class VirtualGamepad {
public:
    enum class VirtualButton {
        ButtonA, ButtonB, ButtonX, ButtonY, StickL, StickR, TriggerL, TriggerR, TriggerZL, TriggerZR, ButtonPlus, ButtonMinus,
        ButtonLeft, ButtonUp, ButtonRight, ButtonDown, ButtonSL, ButtonSR, ButtonHome, ButtonCapture,
    };
    enum class VirtualStick { Left = 0, Right = 1 };
    void SetButtonState(std::size_t player_index, int button_id, bool value);
    void SetButtonState(std::size_t player_index, VirtualButton button_id, bool value) { SetButtonState(player_index, static_cast<int>(button_id), value); }
    void SetStickPosition(std::size_t player_index, int axis_id, float x_value, float y_value);
    void SetStickPosition(std::size_t player_index, VirtualStick axis_id, float x, float y) { SetStickPosition(player_index, static_cast<int>(axis_id), x, y); }
    void SetMotionState(std::size_t, u64, float, float, float, float, float, float) {}
    void ResetControllers();
};
} // namespace InputCommon

// nx2android: Core::HID::HIDCore, only player 1 / handheld
#pragma once
#include "hid_core/frontend/emulated_controller.h"
namespace Core::HID {
class HIDCore {
public:
    EmulatedController* GetEmulatedController(NpadIdType id) { return id == NpadIdType::Player1 || id == NpadIdType::Handheld ? &player1 : nullptr; }
    const EmulatedController* GetEmulatedController(NpadIdType id) const { return id == NpadIdType::Player1 || id == NpadIdType::Handheld ? &player1 : nullptr; }
private:
    EmulatedController player1;
};
} // namespace Core::HID

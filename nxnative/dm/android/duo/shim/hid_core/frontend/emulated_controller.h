// nx2android: player 1's controller as the port's HID sees it (hle_hid.c, through duo_host.cpp)
#pragma once
#include "hid_core/hid_types.h"
namespace Core::HID {
struct AnalogSticks { AnalogStickState left{}; AnalogStickState right{}; };
class EmulatedController {
public:
    bool IsConnected(bool = false) const { return true; }
    NpadButtonState GetNpadButtons() const;
    AnalogSticks GetSticks() const;
};
} // namespace Core::HID

// nx2android
#pragma once
#include "input_common/drivers/virtual_gamepad.h"
namespace InputCommon {
class InputSubsystem {
public:
    VirtualGamepad* GetVirtualGamepad() { return &pad; }
private:
    VirtualGamepad pad;
};
}

// nx2android: the one helper of Eden's hid_util.h the runtime uses
#pragma once
#include "hid_core/hid_types.h"
namespace Service::HID {
constexpr size_t NpadIdTypeToIndex(Core::HID::NpadIdType id) {
    switch (id) {
    case Core::HID::NpadIdType::Player1: return 0; case Core::HID::NpadIdType::Player2: return 1;
    case Core::HID::NpadIdType::Player3: return 2; case Core::HID::NpadIdType::Player4: return 3;
    case Core::HID::NpadIdType::Player5: return 4; case Core::HID::NpadIdType::Player6: return 5;
    case Core::HID::NpadIdType::Player7: return 6; case Core::HID::NpadIdType::Player8: return 7;
    case Core::HID::NpadIdType::Handheld: return 8; default: return 9;
    }
}
}

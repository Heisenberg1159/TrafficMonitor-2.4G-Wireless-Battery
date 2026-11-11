// Factories of the individual protocols. Declare yours here and add a row to
// kProtocols in registry.cpp.
#pragma once

#include <memory>

#include "battery_protocols.h"

std::unique_ptr<BatteryProvider> CreateCompxMouseProvider();
std::unique_ptr<BatteryProvider> CreateMchoseKeyboardProvider();

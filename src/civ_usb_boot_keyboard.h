#pragma once
#include <Arduino.h>
namespace CivUsbBootKeyboard {
// Second *separate* USB HID interface: boot keyboard, 8-byte reports, no ID.
// Only available in the CIV_SWITCH_USB_DUAL diagnostic build.
bool begin();
bool ready();
String diagnostics();
uint32_t reportCount();
bool pressAscii(char character);
bool pressSpecial(uint8_t usage);
void releaseAll();
}

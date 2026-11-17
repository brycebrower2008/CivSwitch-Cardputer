#pragma once
#include <Arduino.h>

namespace CivHid {
enum class Transport : uint8_t { USB = 0, BLE = 1 };
void begin();
void poll();
void startBle();
void setTransport(Transport target);
Transport transport();
bool usbReady();
bool bleReady();
bool ready();
String stateLabel();
bool keyDownAscii(char c);
bool keyDownSpecial(uint8_t code);
void keyUp();
bool gamepadA(bool down);
void releaseAll();
} // namespace CivHid

// Key codes for the few non-printable keys used by the macro.
namespace CivHidKeys {
constexpr uint8_t ESC = 0xB1;   // Arduino USB keyboard KEY_ESC
constexpr uint8_t ENTER = 0xB0; // Arduino USB keyboard KEY_RETURN
}

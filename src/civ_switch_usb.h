#pragma once
#include <Arduino.h>
#if defined(CIV_SWITCH_USB_TEST) || defined(CIV_SWITCH_USB_COMBINED) || defined(CIV_SWITCH_USB_DUAL)
#include <USBHID.h>

// Temporary controller-only USB compatibility image. HID keyboard is
// intentionally disabled in this build until Switch gamepad recognition is verified.
class CivSwitchUsbPad : public USBHIDDevice {
 public:
  CivSwitchUsbPad();
  void begin();
  bool ready();
  bool setA(bool pressed);
  bool setLR(bool pressed);
  bool send();
  void releaseAll();
  uint16_t _onGetDescriptor(uint8_t* dst) override;
  void _onOutput(uint8_t reportId, const uint8_t* data, uint16_t len) override;
 private:
  USBHID hid_;
  uint8_t report_[8];
};
#endif

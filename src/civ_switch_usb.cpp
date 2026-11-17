// Switch-compatible controller-only test. Based on the HID layout documented
// by Gezine/CivSwitch PicoSwitch (MIT). No keyboard interface in this test image.
// USB identifiers 0x0F0D:0x0092 represent the HORI Pokken controller family for
// controller interoperability testing; NOT produced or endorsed by HORI.
#include "civ_switch_usb.h"
#ifdef CIV_SWITCH_USB_TEST
#include <USB.h>
#include <string.h>

namespace {
// Canonical 86-byte 8-byte/no-report-ID gamepad HID descriptor (PicoSwitch).
// Input = uint16 buttons, 4-bit hat + pad, four 8-bit axes, vendor byte.
// 0x0004 is the A button; hat idle=8; all analog axes idle=0x80.
static const uint8_t reportDescriptor[] = {
  0x05,0x01,0x09,0x05,0xA1,0x01,
  0x15,0x00,0x25,0x01,0x35,0x00,0x45,0x01,
  0x75,0x01,0x95,0x10,
  0x05,0x09,0x19,0x01,0x29,0x10,0x81,0x02,
  0x05,0x01,0x25,0x07,0x46,0x3B,0x01,
  0x75,0x04,0x95,0x01,0x65,0x14,0x09,0x39,0x81,0x42,
  0x65,0x00,0x95,0x01,0x81,0x01,
  0x26,0xFF,0x00,0x46,0xFF,0x00,
  0x09,0x30,0x09,0x31,0x09,0x32,0x09,0x35,
  0x75,0x08,0x95,0x04,0x81,0x02,
  0x06,0x00,0xFF,0x09,0x20,0x95,0x01,0x81,0x02,
  0x0A,0x21,0x26,0x95,0x08,0x91,0x02,
  0xC0
};
static_assert(sizeof(reportDescriptor)==86,"PicoSwitch USB gamepad descriptor must be 86 bytes");
}

CivSwitchUsbPad::CivSwitchUsbPad() : hid_(HID_ITF_PROTOCOL_NONE) {
  memset(report_,0,sizeof(report_));
  report_[2]=8; // no D-pad direction
  report_[3]=report_[4]=report_[5]=report_[6]=128;
  USBHID::addDevice(this,sizeof(reportDescriptor));
}
uint16_t CivSwitchUsbPad::_onGetDescriptor(uint8_t* dst) {
  memcpy(dst,reportDescriptor,sizeof(reportDescriptor));
  return sizeof(reportDescriptor);
}
void CivSwitchUsbPad::_onOutput(uint8_t, const uint8_t*, uint16_t) {
  // No rumble/LED behavior is required for the single-button input test.
}
void CivSwitchUsbPad::begin() {
  // Must be set BEFORE USB.begin() so Switch sees them at enumeration.
  USB.VID(0x0F0D);
  USB.PID(0x0092);
  USB.productName("Pokken-compatible Test Pad");
  USB.manufacturerName("CivSwitch Community");
  USB.usbClass(0);
  USB.usbSubClass(0);
  USB.usbProtocol(0);
  hid_.begin();
  USB.begin();
  releaseAll();
}
bool CivSwitchUsbPad::ready() { return hid_.ready(); }
bool CivSwitchUsbPad::send() {
  if (!ready()) return false;
  return hid_.SendReport(0,report_,sizeof(report_),20);
}
bool CivSwitchUsbPad::setA(bool down) {
  if (down) report_[0]|=0x04;
  else report_[0]&=~0x04;
  return send();
}
void CivSwitchUsbPad::releaseAll() {
  report_[0]=0; report_[1]=0; report_[2]=8;
  report_[3]=report_[4]=report_[5]=report_[6]=128;
  report_[7]=0;
  if (ready()) send();
}
#endif

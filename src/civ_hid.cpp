#include "civ_hid.h"
#include <USB.h>
#include <USBHID.h>
#include <USBHIDKeyboard.h>
#include <USBHIDGamepad.h>
#include "civ_switch_usb.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLEAdvertising.h>
#include <BLECharacteristic.h>
#include <string.h>

namespace {
USBHID usbStatus;
#if defined(CIV_SWITCH_USB_TEST) || defined(CIV_SWITCH_USB_COMBINED)
// Retain the byte-exact Nintendo-compatible controller tested on Switch 2.
// The COMBINED build additionally registers a boot-keyboard HID report on the
// same Arduino USB HID interface. Its host compatibility is NOT yet verified.
CivSwitchUsbPad usbTestPad;
uint32_t lastTestReport = 0;
#ifdef CIV_SWITCH_USB_COMBINED
USBHIDKeyboard usbKeyboard;
#endif
#else
USBHIDKeyboard usbKeyboard;
USBHIDGamepad usbGamepad;
#endif
CivHid::Transport selected = CivHid::Transport::USB;
bool usbStarted = false, bleStarted = false;
BLEServer* bleServer = nullptr;
BLEHIDDevice* bleDevice = nullptr;
BLECharacteristic* bleKeyboard = nullptr;
BLECharacteristic* bleGamepad = nullptr;
// Report 1 = standard HID keyboard; report 2 = generic gamepad.
// Generic BLE HID is NOT the proprietary Switch 2 Pro Controller BLE protocol.
const uint8_t bleReportMap[] = {
  0x05,0x01,0x09,0x06,0xA1,0x01,0x85,0x01,
  0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,
  0x75,0x01,0x95,0x08,0x81,0x02, 0x95,0x01,0x75,0x08,0x81,0x01,
  0x19,0x00,0x29,0x65,0x15,0x00,0x25,0x65,0x75,0x08,0x95,0x06,0x81,0x00,
  0x05,0x08,0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,
  0x75,0x01,0x95,0x05,0x91,0x02,0x75,0x03,0x95,0x01,0x91,0x01,0xC0,
  0x05,0x01,0x09,0x05,0xA1,0x01,0x85,0x02,
  0x05,0x09,0x19,0x01,0x29,0x10,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x10,0x81,0x02,
  0x05,0x01,0x09,0x39,0x15,0x00,0x25,0x07,0x35,0x00,0x46,0x3B,0x01,
  0x65,0x14,0x75,0x04,0x95,0x01,0x81,0x42,0x65,0x00,0x75,0x04,0x95,0x01,0x81,0x01,
  0x09,0x30,0x09,0x31,0x09,0x32,0x09,0x35,0x15,0x00,0x26,0xFF,0x00,
  0x75,0x08,0x95,0x04,0x81,0x02,0xC0
};
uint8_t bleKeyReport[8] = {};
uint8_t blePadReport[7] = {0,0,8,128,128,128,128};

bool asciiUsage(char c, uint8_t& modifier, uint8_t& usage) {
  modifier = 0;
  if (c >= 'a' && c <= 'z') { usage = 4 + (c-'a'); return true; }
  if (c >= 'A' && c <= 'Z') { usage = 4 + (c-'A'); modifier=2; return true; }
  if (c >= '1' && c <= '9') { usage = 30 + (c-'1'); return true; }
  if (c == '0') { usage=39; return true; }
  switch (c) {
    case ' ': usage=44; return true;
    case '-': usage=45; return true;
    case '=': usage=46; return true;
    case '[': usage=47; return true;
    case ']': usage=48; return true;
    case '\\': usage=49; return true;
    case ';': usage=51; return true;
    case 39: usage=52; return true;
    case 96: usage=53; return true;
    case ',': usage=54; return true;
    case '.': usage=55; return true;
    case '/': usage=56; return true;
    default: break;
  }
  const char *normal="!@#$%^&*()_+{}|:\"~<>?";
  const uint8_t keys[]={30,31,32,33,34,35,36,37,38,39,45,46,47,48,49,51,52,53,54,55,56};
  const char *hit=strchr(normal,c);
  if (!hit) return false;
  size_t idx=(size_t)(hit-normal);
  if (idx>=sizeof(keys)) return false;
  modifier=2; usage=keys[idx]; return true;
}
void notifyKey() {
  if (!bleStarted || !bleServer || !bleServer->getConnectedCount() || !bleKeyboard) return;
  bleKeyboard->setValue(bleKeyReport,sizeof(bleKeyReport));
  bleKeyboard->notify();
}
void notifyPad() {
  if (!bleStarted || !bleServer || !bleServer->getConnectedCount() || !bleGamepad) return;
  bleGamepad->setValue(blePadReport,sizeof(blePadReport));
  bleGamepad->notify();
}
} // namespace

void CivHid::begin() {
  if (usbStarted) return;
#if defined(CIV_SWITCH_USB_TEST) || defined(CIV_SWITCH_USB_COMBINED)
#ifdef CIV_SWITCH_USB_COMBINED
  // Initialize the keyboard report sender BEFORE USB starts. Its report is
  // HID_REPORT_ID_KEYBOARD (1), while the original gamepad remains report 0.
  usbKeyboard.begin(KeyboardLayout_en_US);
#endif
  usbTestPad.begin();
  usbStarted=true;
#else
  usbKeyboard.begin(KeyboardLayout_en_US);
  usbGamepad.begin();
  USB.productName("CivSwitch Cardputer HID");
  USB.manufacturerName("CivSwitch Community");
  usbStarted=USB.begin();
#endif
  Serial.printf("USB HID started=%d\n",(int)usbStarted);
}
void CivHid::startBle() {
  if (bleStarted) return;
  BLEDevice::init("CivSwitch BLE HID");
  bleServer=BLEDevice::createServer();
  bleDevice=new BLEHIDDevice(bleServer);
  bleKeyboard=bleDevice->inputReport(1);
  bleGamepad=bleDevice->inputReport(2);
  bleDevice->outputReport(1);
  bleDevice->manufacturer("CivSwitch Community");
  bleDevice->pnp(0x02, 0x1234, 0x0001, 0x0001);
  bleDevice->hidInfo(0x00,0x01);
  bleDevice->reportMap(const_cast<uint8_t*>(bleReportMap),sizeof(bleReportMap));
  bleDevice->startServices();
  BLEAdvertising *adv=bleServer->getAdvertising();
  adv->setAppearance(HID_GAMEPAD);
  adv->addServiceUUID(BLEUUID((uint16_t)0x1812));
  adv->start();
  bleStarted=true;
}
void CivHid::setTransport(Transport target) {
  releaseAll();
  selected=target;
  if (target==Transport::BLE && !bleStarted) startBle();
}
CivHid::Transport CivHid::transport() { return selected; }
bool CivHid::usbReady() {
#if defined(CIV_SWITCH_USB_TEST) || defined(CIV_SWITCH_USB_COMBINED)
  return usbStarted && usbTestPad.ready();
#else
  return usbStarted && usbStatus.ready();
#endif
}
bool CivHid::bleReady() { return bleStarted && bleServer && bleServer->getConnectedCount()>0; }
bool CivHid::ready() { return selected==Transport::USB ? usbReady() : bleReady(); }
String CivHid::stateLabel() {
  if (selected==Transport::USB) return usbReady() ? "USB gamepad ready" : "USB gamepad disconnected";
  if (bleReady()) return "BLE HID connected";
  return bleStarted ? "BLE HID advertising" : "BLE HID stopped";
}
bool CivHid::keyDownAscii(char c) {
  if (!ready()) return false;
  if (selected==Transport::USB) {
#ifdef CIV_SWITCH_USB_TEST
    // Controller-only baseline intentionally cannot emit USB text.
    return false;
#else
    return usbKeyboard.press((uint8_t)c)>0;
#endif
  }
  uint8_t mod=0,usage=0;
  if (!asciiUsage(c,mod,usage)) return false;
  memset(bleKeyReport,0,sizeof(bleKeyReport));
  bleKeyReport[0]=mod; bleKeyReport[2]=usage;
  notifyKey(); return true;
}
bool CivHid::keyDownSpecial(uint8_t key) {
  if (!ready()) return false;
  if (selected==Transport::USB) {
#ifdef CIV_SWITCH_USB_TEST
    return false;
#else
    return usbKeyboard.press(key)>0;
#endif
  }
  const uint8_t usage=key==CivHidKeys::ESC ? 41 : (key==CivHidKeys::ENTER ? 40 : 0);
  if (!usage) return false;
  memset(bleKeyReport,0,sizeof(bleKeyReport));
  bleKeyReport[2]=usage; notifyKey(); return true;
}
void CivHid::keyUp() {
  if (selected==Transport::USB) {
#ifndef CIV_SWITCH_USB_TEST
    usbKeyboard.releaseAll();
#endif
  }
  else { memset(bleKeyReport,0,sizeof(bleKeyReport)); notifyKey(); }
}
bool CivHid::gamepadA(bool down) {
  if (!ready()) return false;
  if (selected==Transport::USB) {
#if defined(CIV_SWITCH_USB_TEST) || defined(CIV_SWITCH_USB_COMBINED)
    return usbTestPad.setA(down);
#else
    if (down) return usbGamepad.pressButton(BUTTON_A);
    return usbGamepad.releaseButton(BUTTON_A);
#endif
  }
  blePadReport[0]=down ? 1 : 0; notifyPad(); return true;
}
void CivHid::releaseAll() {
#if defined(CIV_SWITCH_USB_TEST) || defined(CIV_SWITCH_USB_COMBINED)
  if (usbStarted) usbTestPad.releaseAll();
#ifdef CIV_SWITCH_USB_COMBINED
  usbKeyboard.releaseAll();
#endif
#else
  usbKeyboard.releaseAll();
  usbGamepad.releaseButton(BUTTON_A);
#endif
  memset(bleKeyReport,0,sizeof(bleKeyReport));
  blePadReport[0]=blePadReport[1]=0;
  notifyKey(); notifyPad();
}

void CivHid::poll() {
#if defined(CIV_SWITCH_USB_TEST) || defined(CIV_SWITCH_USB_COMBINED)
  if (selected==Transport::USB && usbReady() && millis()-lastTestReport>=15) {
    lastTestReport=millis();
    usbTestPad.send(); // Poll even when neutral, as a wired Switch gamepad does.
  }
#endif
}

bool CivHid::gamepadLR(bool down) {
#if defined(CIV_SWITCH_USB_TEST) || defined(CIV_SWITCH_USB_COMBINED)
  if (selected==Transport::USB && usbReady()) return usbTestPad.setLR(down);
#endif
  return false;
}

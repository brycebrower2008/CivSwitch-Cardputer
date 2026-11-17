// Copyright (c) 2026 CivSwitch-Cardputer contributors.
// Two HID interfaces on ESP32-S3 Arduino 3.3.x without patching prebuilt
// TinyUSB. The built-in TinyUSB HID driver owns the known-good, report-ID-0
// Pokken-compatible gamepad (interface 0); this independent application class
// driver owns a boot keyboard (interface 1).
//
// Arduino's USBHIDKeyboard would register a keyboard report inside the
// same HID interface as the gamepad, which the Switch misinterprets.
// This uses TinyUSB's documented usbd_app_driver_get_cb extension point.
// Experimental: both HID interfaces need hardware validation on Switch 2.
#include "civ_usb_boot_keyboard.h"

#ifdef CIV_SWITCH_USB_DUAL
#include <USB.h>
#include "esp32-hal-tinyusb.h"
#include "device/usbd_pvt.h"
#include "class/hid/hid.h"
#include <cstring>

namespace {
constexpr uint8_t kReportLen = 8;
constexpr uint16_t kKeyboardConfigLen = 9 + 9 + 7 + 7;

// A boot keyboard's own 64-byte HID report descriptor. No report IDs.
constexpr uint8_t kKeyboardReportDescriptor[] = {
    0x05,0x01, 0x09,0x06, 0xA1,0x01,
    0x75,0x01, 0x95,0x08, 0x05,0x07,
    0x19,0xE0, 0x29,0xE7, 0x15,0x00, 0x25,0x01,
    0x81,0x02, 0x95,0x01, 0x75,0x08, 0x81,0x01,
    0x95,0x05, 0x75,0x01, 0x05,0x08,
    0x19,0x01, 0x29,0x05, 0x91,0x02,
    0x95,0x01, 0x75,0x03, 0x91,0x01,
    0x95,0x06, 0x75,0x08, 0x15,0x00,
    0x26,0x91,0x00, 0x05,0x07, 0x19,0x00,
    0x29,0x91, 0x81,0x00, 0xC0
};
static_assert(sizeof(kKeyboardReportDescriptor)==64,
              "PicoSwitch boot keyboard descriptor must be 64 bytes");

uint8_t g_interface=0xFF;
uint8_t g_epIn=0;
uint8_t g_epOut=0;
uint8_t g_idle=0;
uint8_t g_protocol=HID_PROTOCOL_REPORT;
uint8_t g_ledState=0;
bool g_registered=false;
alignas(4) uint8_t g_inReport[kReportLen] = {};
alignas(4) uint8_t g_outReport[kReportLen] = {};
alignas(4) uint8_t g_controlReport[kReportLen] = {};
const uint8_t* g_hidDescriptor = nullptr;

// The Arduino core allocates interface #0 to the registered gamepad
// and interface #1 to the CUSTOM keyboard interface.
uint16_t keyboard_config_descriptor(uint8_t* dst, uint8_t* itf) {
  const uint8_t ep_in = tinyusb_get_free_in_endpoint();
  const uint8_t ep_out = tinyusb_get_free_out_endpoint();
  if (!ep_in || !ep_out) return 0;

  uint8_t config[kKeyboardConfigLen] = {
    0x09, 0x04, *itf, 0x00, 0x02, 0x03, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22,
    static_cast<uint8_t>(sizeof(kKeyboardReportDescriptor)), 0x00,
    0x07, 0x05, static_cast<uint8_t>(0x80 | ep_in), 0x03, 0x08, 0x00, 0x01,
    0x07, 0x05, ep_out, 0x03, 0x08, 0x00, 0x01
  };
  static_assert(sizeof(config)==kKeyboardConfigLen, "Invalid keyboard configuration");
  std::memcpy(dst,config,sizeof(config));
  ++(*itf);
  return sizeof(config);
}

bool usb_xfer(uint8_t ep, uint8_t* bytes, uint16_t len) {
#if (TUSB_VERSION_MAJOR > 0) || (TUSB_VERSION_MINOR >= 19)
  return usbd_edpt_xfer(0,ep,bytes,len,false);
#else
  return usbd_edpt_xfer(0,ep,bytes,len);
#endif
}

void kb_init() {}
bool kb_deinit() { return true; }
void kb_reset(uint8_t) {
  g_interface=0xFF;
  g_epIn=g_epOut=0;
  g_idle=0;
  g_protocol=HID_PROTOCOL_REPORT;
  g_ledState=0;
  g_hidDescriptor=nullptr;
  std::memset(g_inReport,0,sizeof(g_inReport));
  std::memset(g_outReport,0,sizeof(g_outReport));
}

uint16_t kb_open(uint8_t rhport, tusb_desc_interface_t const* intf, uint16_t length) {
  // The built-in HID driver claims the gamepad. Claim only the next keyboard.
  if (intf->bInterfaceClass != TUSB_CLASS_HID ||
      intf->bInterfaceSubClass != HID_SUBCLASS_BOOT ||
      intf->bInterfaceProtocol != HID_ITF_PROTOCOL_KEYBOARD ||
      intf->bInterfaceNumber == 0 ||
      intf->bNumEndpoints != 2 ||
      length < kKeyboardConfigLen) return 0;

  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(intf);
  const uint8_t* hid_desc = bytes + sizeof(tusb_desc_interface_t);
  if (hid_desc[1] != HID_DESC_TYPE_HID) return 0;

  uint8_t in=0,out=0;
  const uint8_t* ep_desc = hid_desc + 9;
  if (!usbd_open_edpt_pair(rhport, ep_desc, 2, TUSB_XFER_INTERRUPT, &out, &in) ||
      !in || !out) return 0;

  g_interface=intf->bInterfaceNumber;
  g_epIn=in;
  g_epOut=out;
  g_hidDescriptor=hid_desc;
  g_protocol=HID_PROTOCOL_REPORT;
  g_idle=0;
  // Listen for keyboard LED updates from the host.
  if (!usb_xfer(g_epOut,g_outReport,sizeof(g_outReport))) return 0;
  return kKeyboardConfigLen;
}

bool kb_control(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
  if (request->bmRequestType_bit.recipient != TUSB_REQ_RCPT_INTERFACE ||
      request->wIndex != g_interface) return false;
  const uint8_t req_type=request->bmRequestType_bit.type;
  if (req_type==TUSB_REQ_TYPE_STANDARD) {
    if (stage != CONTROL_STAGE_SETUP ||
        request->bRequest != TUSB_REQ_GET_DESCRIPTOR) return false;
    switch (tu_u16_high(request->wValue)) {
      case HID_DESC_TYPE_HID:
        return g_hidDescriptor &&
               tud_control_xfer(rhport,request,(void*)g_hidDescriptor,9);
      case HID_DESC_TYPE_REPORT:
        return tud_control_xfer(rhport,request,
                  (void*)kKeyboardReportDescriptor,sizeof(kKeyboardReportDescriptor));
      default: return false;
    }
  }
  if (req_type != TUSB_REQ_TYPE_CLASS) return false;
  switch (request->bRequest) {
    case HID_REQ_CONTROL_GET_PROTOCOL:
      return stage!=CONTROL_STAGE_SETUP ||
          tud_control_xfer(rhport,request,&g_protocol,1);
    case HID_REQ_CONTROL_SET_PROTOCOL:
      if (stage==CONTROL_STAGE_SETUP) return tud_control_status(rhport,request);
      if (stage==CONTROL_STAGE_ACK) g_protocol=(uint8_t)request->wValue;
      return true;
    case HID_REQ_CONTROL_GET_IDLE:
      return stage!=CONTROL_STAGE_SETUP ||
          tud_control_xfer(rhport,request,&g_idle,1);
    case HID_REQ_CONTROL_SET_IDLE:
      if (stage==CONTROL_STAGE_SETUP) {
        g_idle=tu_u16_high(request->wValue);
        return tud_control_status(rhport,request);
      }
      return true;
    case HID_REQ_CONTROL_GET_REPORT:
      if (stage!=CONTROL_STAGE_SETUP) return true;
      if (tu_u16_high(request->wValue)==HID_REPORT_TYPE_INPUT) {
        return tud_control_xfer(rhport,request,g_inReport,sizeof(g_inReport));
      }
      if (tu_u16_high(request->wValue)==HID_REPORT_TYPE_OUTPUT) {
        return tud_control_xfer(rhport,request,&g_ledState,1);
      }
      return false;
    case HID_REQ_CONTROL_SET_REPORT:
      if (request->wLength>sizeof(g_controlReport)) return false;
      if (stage==CONTROL_STAGE_SETUP) {
        return tud_control_xfer(rhport,request,g_controlReport,request->wLength);
      }
      if (stage==CONTROL_STAGE_ACK && request->wLength &&
          tu_u16_high(request->wValue)==HID_REPORT_TYPE_OUTPUT) {
        g_ledState=g_controlReport[0];
      }
      return true;
    default: return false;
  }
}

bool kb_transfer(uint8_t rhport, uint8_t ep, xfer_result_t status, uint32_t count) {
  if (ep==g_epOut) {
    if (status==XFER_RESULT_SUCCESS && count) g_ledState=g_outReport[0];
    return usb_xfer(g_epOut,g_outReport,sizeof(g_outReport));
  }
  return ep==g_epIn;
}

bool send_key(uint8_t modifier,uint8_t usage) {
  if (!CivUsbBootKeyboard::ready()) return false;
  if (!usbd_edpt_claim(0,g_epIn)) return false;
  std::memset(g_inReport,0,sizeof(g_inReport));
  g_inReport[0]=modifier;
  g_inReport[2]=usage;
  if (!usb_xfer(g_epIn,g_inReport,sizeof(g_inReport))) {
    usbd_edpt_release(0,g_epIn);
    return false;
  }
  return true;
}

bool ascii_key(char c,uint8_t& mod,uint8_t& key) {
  mod=0;
  if(c>='a' && c<='z') {key=4+(c-'a');return true;}
  if(c>='A' && c<='Z') {key=4+(c-'A');mod=2;return true;}
  if(c>='1' && c<='9') {key=30+(c-'1');return true;}
  if(c=='0') {key=39;return true;}
  switch(c) {
    case ' ':key=44;return true;
    case '-':key=45;return true;
    case '=':key=46;return true;
    case '[':key=47;return true;
    case ']':key=48;return true;
    case '\\':key=49;return true;
    case ';':key=51;return true;
    case '\'':key=52;return true;
    case 96:key=53;return true;
    case ',':key=54;return true;
    case '.':key=55;return true;
    case '/':key=56;return true;
    default:break;
  }
  static constexpr char shifted[]="!@#$%^&*()_+{}|:\"~<>?";
  static constexpr uint8_t usage[]={30,31,32,33,34,35,36,37,38,39,
                                      45,46,47,48,49,51,52,53,54,55,56};
  static_assert(sizeof(usage)==sizeof(shifted)-1,"Invalid symbol map");
  const char* position=std::strchr(shifted,c);
  if (!position) return false;
  const size_t index=(size_t)(position-shifted);
  if (index>=sizeof(usage)) return false;
  mod=2; key=usage[index];
  return true;
}

const usbd_class_driver_t kKeyboardDriver = []() {
  usbd_class_driver_t driver{};
  driver.name="CivSwitch Boot Keyboard";
  driver.init=kb_init;
#if (TUSB_VERSION_MAJOR > 0) || (TUSB_VERSION_MINOR >= 17)
  driver.deinit=kb_deinit;
#endif
  driver.reset=kb_reset;
  driver.open=kb_open;
  driver.control_xfer_cb=kb_control;
  driver.xfer_cb=kb_transfer;
  driver.sof=nullptr;
  return driver;
}();

} // namespace

extern "C" usbd_class_driver_t const* usbd_app_driver_get_cb(uint8_t* driver_count) {
  *driver_count=1;
  return &kKeyboardDriver;
}

bool CivUsbBootKeyboard::begin() {
  if (g_registered) return true;
  if (tinyusb_enable_interface(USB_INTERFACE_CUSTOM,
                               kKeyboardConfigLen,
                               keyboard_config_descriptor)!=ESP_OK) return false;
  g_registered=true;
  return true;
}
bool CivUsbBootKeyboard::ready() {
  return g_registered && tud_mounted() && g_interface!=0xFF &&
         g_epIn!=0 && !usbd_edpt_busy(0,g_epIn);
}
bool CivUsbBootKeyboard::pressAscii(char character) {
  uint8_t mod=0,key=0;
  return ascii_key(character,mod,key) && send_key(mod,key);
}
bool CivUsbBootKeyboard::pressSpecial(uint8_t usage) {
  return send_key(0,usage);
}
void CivUsbBootKeyboard::releaseAll() {
  send_key(0,0);
}
#else
bool CivUsbBootKeyboard::begin() { return false; }
bool CivUsbBootKeyboard::ready() { return false; }
bool CivUsbBootKeyboard::pressAscii(char) { return false; }
bool CivUsbBootKeyboard::pressSpecial(uint8_t) { return false; }
void CivUsbBootKeyboard::releaseAll() {}
#endif

// Experimental, standalone Switch 2 native BLE pairing/test image.
// Not the main Cardputer Wi-Fi/USB firmware. Licensed under MIT; interoperates
// through espp/switch2_pro 1.4.3, with attribution in README.
// No chat macros and no exploit or payload execution in this test image.
#include <chrono>
#include <system_error>
#include <thread>
#include "driver/gpio.h"
#include "nvs.hpp"
#include "switch2_pro.hpp"
#include "logger.hpp"

using namespace std::chrono_literals;

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This test image is designed for Cardputer-Adv (ESP32-S3)"
#endif
#if !CONFIG_BT_CTRL_BLE_MIN_CONN_INTERVAL_ENABLE
#error "Need an ESP-IDF with the official Switch 2 5ms BLE interval support"
#endif

constexpr gpio_num_t kG0Button = GPIO_NUM_0; // Cardputer-Adv G0 side/boot key
constexpr auto kPoll = 15ms;
constexpr int kLrSelectTicks = 75; // ~1.1 seconds held for Switch pairing UI

extern "C" void app_main() {
  espp::Logger log({.tag = "civswitch_switch2_ble", .level = espp::Logger::Verbosity::INFO});
  log.info("Native Nintendo Switch 2 BLE test. NOT the main Cardputer firmware.");
  log.info("This test build has no on-device LCD UI, Wi-Fi, USB macro, or SD sender.");

  espp::Nvs nvs;
  std::error_code ec;
  nvs.init(ec);
  if (ec) {
    log.error("Cannot initialize persistent BLE bond storage: {}", ec.message());
    return;
  }

  gpio_config_t gpio = {};
  gpio.pin_bit_mask = 1ULL << kG0Button;
  gpio.mode = GPIO_MODE_INPUT;
  gpio.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&gpio) != ESP_OK) {
    log.error("Unable to configure the G0 button.");
    return;
  }

  espp::Switch2Pro controller({
      .device_name = "Pro Controller",
      .wake_console_on_boot_enabled = false,
      .log_level = espp::Logger::Verbosity::INFO,
  });
  if (!controller.init()) {
    log.error("Switch2Pro did not initialize. See the USB serial log.");
    return;
  }
  log.info("Pair on Switch 2: Controllers -> Change Grip/Order or Pair.");
  log.info("Press Cardputer G0 after pairing to send a single controller A input.");
  log.info("Hold G0 for 4 seconds when disconnected to reset the pairing bond.");

  espp::switch2::Pro2InputReport report;
  report.set_power(9, false, false); // full battery, never reports charging
  bool prevStreaming = false;
  bool prevPressed = false;
  bool resetPerformed = false;
  int lrTicks = 0;
  int logTicks = 0;
  std::chrono::steady_clock::time_point heldSince = {};

  while (true) {
    const bool pressed = gpio_get_level(kG0Button) == 0;
    const bool pressEdge = pressed && !prevPressed;
    const bool releaseEdge = !pressed && prevPressed;
    prevPressed = pressed;

    if (pressEdge) {
      heldSince = std::chrono::steady_clock::now();
      resetPerformed = false;
      if (!controller.is_connected()) {
        if (controller.wake_console()) log.info("G0: sent wake advertisement");
      }
    }
    if (releaseEdge) heldSince = {};

    // Held-key reset only when disconnected, to avoid wiping a live bond.
    if (pressed && !resetPerformed && !controller.is_connected() &&
        heldSince != std::chrono::steady_clock::time_point{} &&
        (std::chrono::steady_clock::now() - heldSince) >= 4s) {
      if (controller.clear_bond()) {
        log.info("G0 held: erased pairing bond; ready for new Switch 2 pairing");
      } else {
        log.warn("Failed to erase pairing bond");
      }
      resetPerformed = true;
    }

    const bool streaming = controller.is_input_streaming();
    if (streaming && !prevStreaming) {
      log.info("Encrypted input reports active; sending L+R selection for 1 second");
      lrTicks = kLrSelectTicks;
    }
    prevStreaming = streaming;

    // Only send intentional G0 presses after pairing. No automatic A spam.
    report.set_a(pressed && streaming);
    report.set_l(streaming && lrTicks > 0);
    report.set_r(streaming && lrTicks > 0);
    report.set_left_stick(0.f, 0.f);
    report.set_right_stick(0.f, 0.f);
    controller.set_input_report(report);
    if (lrTicks > 0) --lrTicks;

    if (++logTicks >= 66) {
      logTicks = 0;
      log.info("paired={} connected={} streaming={} G0(A)={}",
               controller.is_paired(), controller.is_connected(), streaming, pressed);
    }
    std::this_thread::sleep_for(kPoll);
  }
}

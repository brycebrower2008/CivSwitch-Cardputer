# Native Switch 2 Bluetooth test firmware (experimental)

This folder is a **separate ESP-IDF 6.1 program** for the Cardputer-Adv (ESP32-S3). It uses the upstream [espp/switch2_pro 1.4.3](https://components.espressif.com/components/espp/switch2_pro) driver, which implements the Switch 2 Pro Controller's proprietary BLE GATT, pairing handshake, bond storage, and continuous input reports. The upstream author reports successful tests on ESP32-S3 with a real Switch 2.

**Important:** This is a **BLE pairing/controller test image, not a replacement for the main CivSwitch-Cardputer firmware**. Because of the ESP-IDF/Arduino framework incompatibility, it does *not yet* include the original Cardputer screen, Wi-Fi, SD sender, USB keyboard, or exploit bootstrap. The LCD will be blank. The project keeps the original USB firmware on `main` untouched, so you can test USB before choosing whether to flash this **separate image**. Compiling this code does not prove the Switch 2 accepts it on your particular device.

## Build and test

- Target hardware: **M5Stack Cardputer-Adv only**, ESP32-S3FN8, 8MB flash, G0 button on GPIO0.
- Toolchain: ESP-IDF **v6.1**, target `esp32s3`, registry dependencies pinned to `espp/switch2_pro==1.4.3`.
- Build with `cd ble-switch2 && idf.py set-target esp32s3 && idf.py build && idf.py merge-bin -o build/ble-switch2-fullflash.bin -f raw`.
- The independent GitHub Actions workflow produces `ble-switch2-fullflash.bin` on this feature branch. This is a **merged flash image**, unlike the application's `firmware.bin` in the Arduino build.

**Warning:** Flashing this merged image replaces the installed firmware. Although its NVS partition is not intentionally erased, the new NVS initialization or an incompatible partition scheme can affect stored settings. Back up what you need and use a reputable ESP32-S3 flashing tool at **address 0x0**. Keep the original Cardputer USB firmware available for re-flashing after this test.

1. Flash the merged image using an ESP32-S3 flasher, set address `0x0`, reset the Cardputer, and leave it powered on (side switch ON).
2. On Switch 2 open **Controllers → Change Grip/Order**, then look for a Pro Controller pairing. The firmware advertises as **Pro Controller** using Nintendo-compatible custom BLE advertising, not generic HID.
3. After pairing, the program automatically holds **L+R briefly** to pass the controller selection screen.
4. On the Cardputer, tap the physical **G0** (Boot) button to send **A**. This tests the controller directly; it **does not type on the Switch** or trigger any exploit. You can use Switch 2's controller-input test screen to verify A.
5. The paired console should reconnect automatically on future boots. A tap on G0 while disconnected sends a wake advertisement. If pairing becomes stuck after removing the controller on the console, **hold G0 for 4 seconds while disconnected** to clear the saved bond and allow fresh pairing.

For debugging, ESP-IDF logs the pairing, connection and input streaming to **USB Serial/JTAG** if plugged into a PC for monitoring, but a PC is not required during normal Bluetooth pairing.

## Limitations and integration plan

The upstream [PicoSwitch](https://github.com/Gezine/CivSwitch/tree/main/PicoSwitch) Bluetooth implementation is **Bluetooth Classic for original Switch**, so it cannot be used on an ESP32-S3 or directly on a Switch 2. This experimental native firmware instead uses [espp's Switch2Pro implementation](https://github.com/esp-cpp/espp/tree/main/components/switch2_pro) for the Switch 2's custom BLE GATT. That upstream code depends on modern ESP-IDF, including the **official support for the console's 5 ms BLE interval**. Switching the main Arduino/PlatformIO firmware to this ESP-IDF stack (or creating a cross-framework bridge) is a separate integration task. Only after that should we wire the Cardputer's built-in TCA8418 keyboard, LCD UI, and microSD bootstrap to the native BLE emulator.

This BLE test image **does not run the exploit**. Do not interpret BLE pairing or button recognition as evidence of successful exploit execution. Source attribution: espp is an MIT-licensed third-party dependency fetched by the ESP-IDF Component Manager; see its repository for copyright and license terms.

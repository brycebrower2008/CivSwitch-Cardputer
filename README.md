# CivSwitch Cardputer-Adv (development firmware)

A Wi-Fi and local diagnostics front-end for M5Stack **Cardputer-Adv**.

> **Status:** This firmware does **not** contain the Switch 2 jailbreak, the kaiHEN sender, exploit execution, persistence installation, or SD-payload transmission. Port 9026 probes are **connectivity tests only**. A microSD payload file can be detected and inspected, but is **never executed or sent**.

## On-device configuration — no phone needed

All settings use the Cardputer keyboard. Original display rotation (0) is retained.

| Screen | Controls |
| --- | --- |
| Home | A = Setup; B = Diagnostics; C = TCP test; R = Wi-Fi reconnect |
| Setup | W / S = move; Enter = select; X = Home |
| Wi-Fi networks | W / S = move; Enter = connect; M = manual SSID; X = back |
| Text editor | Type; Backspace removes; Enter saves; Fn + ESC cancels |
| Diagnostics | A = Home; C = retest; D = SD status |
| SD status | C = rescan; A = Home |

On first boot, press **A → Scan Wi-Fi networks**, choose your network, and type the password. The ESP32 Wi-Fi stack stores saved credentials for later boots. Under Setup → **Set Switch / PC IP**, enter your Switch or test-PC IPv4 address. It is stored in NVS. No WiFiManager/captive portal is used.

## microSD payload file

Place your own binary file on a FAT32 microSD card at either path:

- /civswitch/payload.bin (preferred)
- /payload.bin (fallback)

The Cardputer reports its presence, size and first few bytes. **The firmware does not transmit it.** This repo doesn't bundle or generate an exploit payload. SD SPI pins: CLK 40, MISO 39, MOSI 14, CS 12.

Check on-device with **A → Check microSD payload** or **B → D**.

## Windows PC test: test.bat

The included **test.bat** starts a PowerShell-based fake TCP port 9026 endpoint. It is a **connectivity emulator only**, not a real Switch emulator or a firmware compilation test.

1. Put the Cardputer and Windows PC on the same LAN/Wi-Fi.
2. Double-click test.bat on the PC. Allow the Windows Firewall prompt on Private networks if shown.
3. Note the PC's IPv4 address displayed in the command window.
4. On the Cardputer select **A → Set Switch / PC IP**, and enter the PC's IPv4 address.
5. Press **C** on the Cardputer Home screen. The PC logs an incoming connection, and the Cardputer should show **PC EMULATOR**.
6. Ctrl+C stops the emulator.

The test proves TCP connectivity on port 9026, not that a Switch payload or exploit works.

## Automatic GitHub Releases

Pushes to **main**, including new commits, and manual Actions runs produce a successful-build Actions artifact AND a development prerelease tagged **dev-N (GitHub run number)**, each containing firmware.bin. A version tag beginning in **v** (for example v0.1.0) produces a regular versioned Release.

- Releases: https://github.com/brycebrower2008/CivSwitch-Cardputer/releases
- Actions: https://github.com/brycebrower2008/CivSwitch-Cardputer/actions

The compiled binary is an **ESP32-S3 application image**. Flash it using PlatformIO or the correct bootloader/partition/offset flashing procedure. Do not mistake it for a complete merged flash image. Passing CI does not prove the hardware runs correctly.

### Local build

Install PlatformIO, then run:

    pio run -e cardputer-adv
    pio run -e cardputer-adv -t upload

Application binary: .pio/build/cardputer-adv/firmware.bin

## Technical

- M5Stack Cardputer-Adv ESP32-S3, 8MB flash
- Remote endpoint TCP diagnostic: port 9026
- Progress bar = network connectivity only
- SD file = detected and previewed, not transmitted
- Upstream reference: https://github.com/s8n/CivSwitchFS-kaiHEN

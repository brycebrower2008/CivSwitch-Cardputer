# CivSwitch Cardputer-Adv (development firmware)

A Wi-Fi and local diagnostics front-end for M5Stack **Cardputer-Adv**.

> **Status:** The firmware now has an **experimental standalone upstream-JavaScript sender**: it streams a selected prebundled JavaScript program from the **Cardputer's** microSD to an already-running CivSwitch Remote JS Loader on the Switch over TCP 9026. The exploit code executes on the Switch, not on the ESP32. **Hardware and exploit success are not verified.** It does not install CivSwitch persistence or stage kaiHEN files on the Switch SD. Sending may corrupt saves or crash your game; use only if you understand the upstream requirements.

## On-device configuration — no phone needed

All settings use the Cardputer keyboard. Original display rotation (0) is retained.

| Screen | Controls |
| --- | --- |
| Home | A = Setup; B = Diagnostics; C = TCP test; R = Wi-Fi reconnect; **P = JS payloads; H = HID** |
| Setup | W / S = move; Enter = select; X = Home |
| Wi-Fi networks | W / S = move; Enter = connect; M = manual SSID; X = back |
| Text editor | Type; Backspace removes; Enter saves; Tab cancels |
| Diagnostics | A = Home; C = retest; D = SD status |
| SD status | C = rescan; A = Home |
| JS payload list | W/S = select; Enter = preflight; X = Home |
| JS confirmation | Y = send selected script; X = cancel |
| JS running | X = abort (remote state may be unknown); USB serial outputs full remote log |
| JS result | X = list; A = Home |
| HID | U = USB; L = generic BLE; T = send one A test; M = macro; X = Home |
| HID confirmation | Y = run macro; X = cancel |
| HID running | X = abort immediately |

On first boot, press **A → Scan Wi-Fi networks**, choose your network, and type the password. The ESP32 Wi-Fi stack stores saved credentials for later boots. Under Setup → **Set Switch / PC IP**, enter your Switch or test-PC IPv4 address. It is stored in NVS. No WiFiManager/captive portal is used.

## USB HID and Bluetooth controls (experimental)

The M5Stack Cardputer-Adv has a **native USB-C OTG device interface**. The firmware adds USB keyboard + generic USB gamepad HID, and an optional **generic BLE HID-over-GATT keyboard/gamepad**. These are **not yet proven compatible with the Switch 2**. In particular, the generic USB gamepad descriptor is not an exact port of PicoSwitch's dual-interface vendor controller; the Switch 2 Pro Controller uses a **proprietary BLE GATT protocol**, not generic BLE HID. The ESP32-S3 cannot use Bluetooth Classic.

### First test (no keyboard test necessary)

1. Flash the firmware from the latest successful GitHub Release. The original screen orientation is preserved.
2. Connect the Cardputer's native USB-C port to the **Switch 2 USB port** using a data cable.
3. Press **H** on the Cardputer Home screen.
4. Press **U** (USB). The device displays **USB HID enumerated** if the host accepted USB descriptors (this doesn't prove the console accepts gamepad input).
5. Focus a harmless Switch menu item; press **T** to send **one controller A tap**, with **no keyboard text**. Check if the menu reacts.

Please share whether USB says enumerated and whether the Switch reacts to A. A generic USB gamepad might enumerate but still be ignored by the Switch; if so, its descriptor must be replaced with a Switch-specific one.

### Bluetooth mode

Press **H → L** to start advertising a BLE device called **CivSwitch BLE HID**. This is generic BLE HID keyboard/gamepad, not the Switch 2's custom Pro Controller BLE pairing. The device shows advertising/connected state. A Switch 2 may refuse to pair; **this does not mean ESP32-S3 Bluetooth is defective**. True wireless Switch 2 controller support needs a Nintendo BLE GATT stack and compatible ESP-IDF version, which are not included in this build. Use USB until that support is implemented and hardware-verified.

### SD HID bootstrap

Successful Releases include **sd-payloads.zip**, with the **/civswitch/bootstrap.txt** file generated from the pinned [PicoSwitch](https://github.com/Gezine/CivSwitch) compressed chat-XSS input. Put the entire civswitch directory on the **Cardputer** microSD beside the FS-kaiHEN JavaScript files.

After checking USB/controller A works, start the USB HID chat macro using **H → M → Y**. This is a nonblocking, explicitly confirmed replay of the upstream gamepad A, Esc, text, and Enter input sequence to load CivSwitch Remote JS Loader in Civilization VII lobby chat. It can take around **20 minutes**. **X** aborts, potentially leaving partially entered chat text. It never starts automatically when USB is plugged in.

**Risk:** It can trigger the exploit and damage game saves on unsupported configurations. Back up saves and follow upstream setup/compatibility warnings. A completed macro only means input reports were sent; it is **not proof** that CivSwitch/FS-kaiHEN succeeded or that TCP port 9026 is active.

## FS-kaiHEN JS sender (experimental, no PC at runtime)

The original CivSwitchFS-kaiHEN project uses Node.js to **build and send** programs; its actual JS executes within an already-running Switch 2 **CivSwitch Remote JS Loader**. This Cardputer firmware implements the *send* portion: it reads the prebuilt JS program in 1 KB chunks from microSD, writes it to port 9026, half-closes the TCP connection to signal end-of-program, and displays remote output. The JS is generated on GitHub Actions, so Node.js is not needed on the Cardputer or at runtime.

**This does not install or launch the Remote JS Loader in Civilization VII.** Follow upstream CivSwitch prerequisites independently. Upstream has only tested specific Switch 2 firmware/game combinations; see its README. The risk of filesystem/save corruption is real.

Download **sd-payloads.zip** from the same GitHub Release as firmware.bin; extract its civswitch directory to the root of the **Cardputer's** microSD (FAT32):

| File | Purpose | Requirements |
| --- | --- | --- |
| /civswitch/fs.js | FS promotion payload | Remote JS Loader waiting on port 9026 |
| /civswitch/chain.js | FS promotion then kaiHEN in one send | Remote loader plus kaiHEN files **already on Switch SD** |
| /civswitch/chain-sd.js | FS promotion; loads kaiHEN stage from **Switch SD** | /kaihen/payload.js already on Switch SD |
| /civswitch/kaihen.js | Start kaiHEN with an existing promoted FS session | First run fs.js successfully on same console boot; Switch SD files |

Select **P** from Home, choose a script using W/S, press Enter to preflight, and **Y** to confirm transmission. The sender checks file size against the loader's 500 KB limit, shows bytes transmitted and last console message, and only reports success when it sees an explicit upstream success log marker. Otherwise the outcome is unverified. **The PC test.bat emulator cannot execute JS and never counts as exploit success.**

The Cardputer microSD and Switch microSD are **two separate cards**. This firmware does not transfer kaiHEN binaries to the Switch's SD or install the persisted Civ VII save. Those operations still require an independently completed upstream setup. The loader may hang, reject scripts, or crash on unsupported firmware; the hardware/console interaction has not been tested.

The builds are pinned to a specific upstream revision and the Release includes **upstream-source.zip** for corresponding source and licensing notices.

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

Pushes to **main**, including new commits, and manual Actions runs produce a successful-build Actions artifact AND a development prerelease tagged **dev-N (GitHub run number)**, containing firmware.bin, sd-payloads.zip, upstream-source.zip and picoswitch-source.zip. A version tag beginning in **v** (for example v0.1.0) produces a regular versioned Release.

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
- Legacy /civswitch/payload.bin file = detected and previewed, not transmitted
- Prebundled /civswitch/*.js scripts = transmitted only after explicit confirmation
- Upstream reference: https://github.com/s8n/CivSwitchFS-kaiHEN

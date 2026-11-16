# CivSwitch Cardputer-Adv (Switch 2) — standalone UI/diagnostic shell

This project is a standalone ESP32-S3 firmware for the M5Stack Cardputer-Adv.
It provides a small UI, Wi-Fi provisioning, connection status, a progress
indicator, and diagnostics for the CivSwitch Remote JS Loader endpoint.

## Important scope limitation

This is **not** a Cardputer port of the CivSwitchFS-kaiHEN exploit. The upstream
CivSwitchFS-kaiHEN repository currently requires Node.js 20+ and npm for its
sender, persistence installer, and kaiHEN staging/chain tools. It expects the
CivSwitch Remote JS Loader to already be running on the Switch 2 and reachable
on the local network. The exploit payload and kaiHEN chain have not been
ported to ESP32-S3 here.

The firmware intentionally does not send exploit payloads, modify Switch save
data, install persistence, or claim that a TCP connection means the exploit
worked. A successful port-9026 check only establishes that a TCP service
accepted a connection.

Upstream also warns that its filesystem vulnerability can corrupt save data.
Read its current instructions and risks before using that project:
https://github.com/s8n/CivSwitchFS-kaiHEN

## Features

- Cardputer-Adv display UI
- Wi-Fi captive portal for entering network credentials
- Saved Wi-Fi credentials via WiFiManager
- Wi-Fi connection and RSSI status
- TCP reachability test for port 9026
- Real status-based progress indicator (not simulated exploit progress)
- Diagnostics counters and last-check result
- Keyboard shortcuts:
  - `A`: Wi-Fi setup / return home
  - `B`: diagnostics
  - `C`: run diagnostics again
  - `R`: reconnect Wi-Fi

## Build

1. Install PlatformIO (VS Code extension or CLI).
2. Open this folder as a PlatformIO project.
3. Build and upload:

   ```sh
   pio run
   pio run -t upload
   pio device monitor
   ```

The PlatformIO environment targets an ESP32-S3 dev board definition with
8 MB flash. The Cardputer-Adv hardware is based on ESP32-S3; verify the
board profile and flash configuration against your exact Cardputer-Adv
revision before flashing.

## Wi-Fi behavior

The Cardputer joins the same LAN as the Switch 2. If no saved credentials
exist, it starts a setup AP named `CivSwitch-Cardputer`; connect a phone or
computer to it and browse to `192.168.4.1`.

The current diagnostic probes the configured Wi-Fi gateway on TCP port 9026.
If your Switch has a different IP from the gateway, this check will not find
it. This UI shell does not yet include a console-IP entry screen or an
application-level loader handshake.

## Next engineering steps for a real standalone port

A real standalone implementation needs, at minimum:

1. A documented and stable transport protocol for the loader and console IP
   selection/discovery.
2. A complete embedded replacement for the upstream Node.js sender and
   kaiHEN staging tools.
3. Porting and validating the actual CivSwitchFS exploit runtime and payload
   build for ESP32-S3, including memory, storage, and timing constraints.
4. A safe recovery strategy and extensive testing against the exact Switch
   firmware, Civ7 build, and kaiHEN release.

None of those exploit-porting steps are implemented by this diagnostic shell.

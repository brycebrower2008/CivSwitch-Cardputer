# Dual USB HID diagnostic build

The USB controller-only firmware is the **known-working** fallback. The
two-interface diagnostic firmware is intentionally isolated on this branch.

The original `usb-dual-test-1` used separate gamepad/keyboard descriptors but
the keyboard was not recognized on a Windows PC or the Nintendo Switch 2.
The next builds simplify the boot keyboard to **one interrupt-IN endpoint**
(LEDs via optional USB control SET_REPORT), which is valid for a USB boot keyboard.

## On-device USB diagnostics

Open **H → U** on the Cardputer. The device shows the USB controller status
and a diagnostics line in this form:

`D:1 M:1 I:1 S:4 A:1 E:0`

- **D**: keyboard interface registered before USB started (1 is expected).
- **M**: TinyUSB device mounted/configured by host (1 is expected after plugging into PC or Switch).
- **I**: keyboard USB interface number (**1** is expected; **255** means no successful keyboard driver open).
- **S**: keyboard class driver open stage (**4** means the second interface and its endpoint opened; **0-3** means incomplete).
- **A**: number of times TinyUSB attempted to open the keyboard descriptor (expect 1+ if it recognized the keyboard interface).
- **E**: keyboard report transfer errors (expect 0).
- **Key TX**: number of successfully *queued* keyboard reports, NOT proof the host accepted typed text.

### Test order

1. Flash the latest successful **`usb-dual-test-*`** app firmware using the same
   method/offset used for the working controller-only firmware. The
   `firmware-dual-usb-test.bin` asset is **not** a full-flash image.
2. Power the Cardputer, connect its USB-C device port to a **Windows PC** with
   the same data-capable cable that worked on the Switch.
3. Open **H → U**, record the entire diagnostics line.
4. In Windows Device Manager, expand **Keyboards** and
   **Human Interface Devices**. Check for a **HID Keyboard Device**.
5. In Notepad click in a blank document, press **K** on the Cardputer while
   its HID menu is open. Only lowercase `k` is sent, with no Enter.
6. If Windows recognizes the keyboard and types `k`, repeat the controller
   **J (L+R)** and **T (A)** tests on Switch 2 before trying keyboard `K`
   in a harmless empty text box. Entering a real Wi-Fi password is unnecessary.

**Do not run the exploit**: the Civ VII chat bootstrap macro is disabled in
this experimental dual-HID diagnostic image.

If the diagnostics display **D:1 M:1 I:255**, the Cardputer's USB controller
is mounted but the keyboard *class driver* never opened. If it shows
**I:1 S:4**, the keyboard interface opened; then focus on report delivery and
host recognition. This distinction helps find a software fault without guessing.

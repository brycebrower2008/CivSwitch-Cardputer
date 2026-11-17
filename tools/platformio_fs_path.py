# PlatformIO's ESP32 Arduino USB library builds its USBHostMSC.cpp translation
# unit even for device-only HID. That file includes USBMSCFS.h -> FS.h, but
# PlatformIO 6.x LDF does not propagate the sibling FS include directory when
# compiling the USB library itself. Add it to the global compiler include path.
Import("env")
from pathlib import Path

framework = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if not framework:
    raise RuntimeError("Unable to locate Arduino-ESP32 framework package")
fs_include = Path(framework) / "libraries" / "FS" / "src"
if not fs_include.is_dir():
    raise RuntimeError("Arduino-ESP32 FS headers missing: " + str(fs_include))
env.Append(CPPPATH=[str(fs_include)])
print("[civswitch] USB/FS compiler include: " + str(fs_include))

# PlatformIO 6.x LDF only propagates direct library dependencies in some
# ESP32 Arduino 3.3.x builds. USB compiles USBHostMSC.cpp even for HID-only
# devices; this pulls FS.h, which itself pulls WiFiGeneric.h -> Network.h.
# Their Arduino framework include directories must be available to all TUs.
Import("env")
from pathlib import Path

framework=env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if not framework:
    raise RuntimeError("Cannot find Arduino ESP32 framework")
root=Path(framework)/"libraries"
for library in ("FS", "Networking", "Network"):
    include=root/library/"src"
    if include.is_dir():
        env.Append(CPPPATH=[str(include)])
        print("[civswitch] Arduino dependency headers: "+str(include))
if not (root/"FS"/"src"/"FS.h").exists():
    raise RuntimeError("Arduino FS headers are missing")
if not ((root/"Networking"/"src"/"Network.h").exists() or (root/"Network"/"src"/"Network.h").exists()):
    raise RuntimeError("Arduino Network.h is missing")

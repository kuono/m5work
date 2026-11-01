# M5-MicroHs Remote Vibration Monitoring System (v1.0.0)

[日本語](README.md) | [English](README_en.md) | [简体中文](README_zh.md)

A highly reliable remote condition monitoring system bridging an M5StickC Plus2 (Vibration Sensor) and an M5Core2 (Central Monitor) via ESP-NOW.  
The core evaluation logic utilizes **MicroHs** (a Haskell implementation for embedded systems) transpiled into C, ensuring a pure functional state machine architecture.

---

## Key Features

- **Functional Reactive Core**: State transition logic for both the sensor and receiver is written in Haskell. Features like a 2.0-second hold delay and 4-level sensitivity presets are implemented as pure functions.
- **Low-Latency & Robust Communication**: Direct peer-to-peer communication using ESP-NOW (with AES encryption). No Wi-Fi router required.
- **Dynamic UI Layout**: UI coordinates and colors are loaded dynamically from `layout.txt` located in LittleFS (with SD card fallback) on the receiver.
- **Reproducible Build Pipeline**: Automated via `Makefile`, seamlessly handling Haskell transpilation, PlatformIO firmware building, device flashing, and documentation generation.

---

## System Architecture

```text
[ Sensor Node ]
  M5StickC Plus2 (IMU: MPU6886)
      │
      ▼ (Transpilation)
  Haskell Core (Logic.hs -> Logic.c)
      │
      ▼ [ ESP-NOW (Encrypted) ]
      │
[ Receiver Node ]
  M5Core2 (TFT LCD / Touch / LittleFS)
      │
      ▼ (Transpilation)
  Haskell Monitor (MonitorLogic.hs -> MonitorLogic.c)
```

---

## Directory Structure

```text
.
├── Makefile                # Build automation pipeline
├── build_docs.sh           # Documentation generation script
├── common/                 # Shared headers and runtime
│   ├── espnow/             # ESP-NOW configuration (secret templates)
│   └── microhs_runtime/    # Patched MicroHs runtime for ESP32 (eval.c)
├── core2_receiver/         # Receiver (M5Core2) PlatformIO project
├── m5stick_sensor/         # Sensor (M5StickC Plus2) PlatformIO project
├── haskell/                # Haskell source code for core logic
│   ├── Logic.hs            # Sensor logic
│   └── MonitorLogic.hs     # Receiver logic
└── docs/                   # System specifications and generated docs
```

---

## Prerequisites

- **Host Environment**: macOS / Linux (zsh or bash)
- **Build Tools**:
  - [PlatformIO Core (CLI)](https://platformio.org/)
  - GNU Make
  - (Optional) MicroHs (`mhs`) — Only required if editing Haskell logic
  - (Optional) Pandoc, Haddock — For generating documentation

---

## Quick Start

### 1. Configure Communication Settings
For security, ESP-NOW PMK/LMK and MAC addresses are excluded from the repository. Create your configuration from the template:

```bash
cp common/espnow/espnow_config.h.example common/espnow/espnow_config.h
```
Edit `common/espnow/espnow_config.h` to set your encryption keys and target MAC addresses.

### 2. Build the Firmware
Run `make all` in the project root to build firmware for both devices.

```bash
make all
```

### 3. Flash to Devices

Connect your devices via USB and upload the firmware:

```bash
# Upload to Sensor (M5StickC Plus2)
make upload-sensor

# Upload to Receiver (M5Core2)
make upload-receiver

# Upload Receiver UI config to LittleFS
make upload-fs
```

---

## Makefile Commands

| Command | Description |
|---|---|
| `make all` | Build both sensor and receiver firmware |
| `make haskell` | Transpile `haskell/*.hs` to C code |
| `make sensor` | Build sensor firmware only |
| `make receiver` | Build receiver firmware only |
| `make upload-sensor` | Flash sensor firmware |
| `make upload-receiver` | Flash receiver firmware |
| `make upload-fs` | Flash `data/layout.txt` to receiver's LittleFS |
| `make monitor-sensor` | Open sensor serial monitor |
| `make monitor-receiver` | Open receiver serial monitor |
| `make doc` | Regenerate documentation (HTML/PDF & API) |
| `make clean` | Remove PlatformIO cache and intermediate C files |

---

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
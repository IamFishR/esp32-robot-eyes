# ESP32 Robot Eyes

Animated robot eye display on a 0.96" OLED with NTP time and WiFi OTA updates.

## Hardware

- ESP32-D0WD-V3 devkit (CP2102 USB-UART)
- 0.96" SSD1306 OLED (128x64, I2C)

### OLED Wiring

| OLED Pin | ESP32 Pin |
|----------|-----------|
| VCC      | 3.3V      |
| GND      | GND       |
| SDA      | GPIO 21   |
| SCL      | GPIO 22   |

## Features

- Animated robot eyes (blink, random look directions)
- Current time (IST) from NTP at bottom of screen
- WiFi OTA updates via GitHub releases
- Web UI to trigger OTA instantly from browser

## OTA Updates (No USB Required)

### Trigger update

From browser: visit `http://192.168.1.7/` and click **Check for OTA Update**.

From terminal:
```bash
curl http://192.168.1.7/update
```

The ESP32 also checks GitHub automatically every hour.

### Release a new version

1. Edit `esp32-robot-eyes.ino`, bump `FIRMWARE_VERSION` (e.g. `"1.0.5"`)
2. Compile:
   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32 \
     --output-dir /tmp/esp32-build \
     /home/rakesh/Dev/esp32-robot-eyes/esp32-robot-eyes
   cp /tmp/esp32-build/esp32-robot-eyes.ino.bin /tmp/esp32-build/firmware.bin
   ```
3. Publish release:
   ```bash
   gh release create 1.0.5 /tmp/esp32-build/firmware.bin --title "v1.0.5" --notes "What changed"
   ```
4. Trigger: `curl http://192.168.1.7/update` — ESP32 downloads, flashes itself, reboots

> The release asset **must be named `firmware.bin`** — the OTA code looks for this exact filename.

> **Verified working** — OTA tested successfully from v1.0.3 → v1.0.4 via WiFi.

## First-Time USB Flash

Only needed once (or to recover a bricked board):

```bash
# Compile
arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir /tmp/esp32-build ./esp32-robot-eyes

# Flash (use --no-stub for noisy cables, flash merged binary at 0x0)
sudo chmod 666 /dev/ttyUSB0
python3 -m esptool --chip esp32 --port /dev/ttyUSB0 --baud 115200 --no-stub \
  write-flash -z 0x0 /tmp/esp32-build/esp32-robot-eyes.ino.merged.bin
```

## Dependencies

Install arduino-cli (installed at `~/.local/bin/arduino-cli` on this machine):
```bash
curl -fsSL https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Linux_64bit.tar.gz -o /tmp/arduino-cli.tar.gz
tar -xzf /tmp/arduino-cli.tar.gz -C ~/.local/bin/
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install "Adafruit SSD1306" "Adafruit GFX Library" "ArduinoJson"
```

## WiFi / Config

Edit these in `esp32-robot-eyes.ino`:
```cpp
const char* ssid     = "your_wifi_name";
const char* password = "your_wifi_password";
#define GITHUB_USER  "IamFishR"
#define GITHUB_REPO  "esp32-robot-eyes"
```

## Troubleshooting

| Problem | Fix |
|---------|-----|
| Brownout reset loop | Already fixed via `initVariant()`. If it recurs, use a better USB cable or powered hub. |
| OLED not showing | Check wiring. Firmware tries both `0x3C` and `0x3D` I2C addresses. |
| OTA not triggering | Ensure release asset is named exactly `firmware.bin`. Check version string is newer. |
| USB flash fails with noise error | Use `--no-stub` flag and flash `merged.bin` at `0x0`. |
| Web server connection refused | ESP32 not on WiFi yet, or wait ~15s after boot for it to connect. |

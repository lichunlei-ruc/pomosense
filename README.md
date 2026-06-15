# PomoSense

A Pomodoro timer for [M5Cardputer](https://docs.m5stack.com/en/core/Cardputer) with orientation-based auto-start, Monica flip-clock style display, Matrix/Rain backgrounds, and WiFi NTP time sync.

**PomoSense** = **Pomo**doro + **Sense** (orientation sensing). Place your Cardputer in different directions to automatically start the corresponding timer.

## Features

- **Orientation-based timer**: Place the device UP/DOWN/LEFT/RIGHT to auto-start 25/60/5/15 minute countdowns
- **Monica flip-clock display**: Card-style digit display with Morandi color palette
- **Background effects**: Matrix code rain, raindrop particles, or none (press B to switch)
- **WiFi NTP sync**: Internet time displayed on idle screen with persistent WiFi credentials
- **Auto-continuous mode**: 25min focus → 5min break → 25min focus cycle
- **Breathing progress bar**: Visual progress indicator around screen edges
- **Low battery warning**: Blinks when battery is below 20%

## Hardware

- M5Cardputer (ESP32-S3, 1.14" 240×135 IPS display, IMU, keyboard)

## Quick Start

### Flash firmware

Download `bin/pomosense.bin` and flash using [M5Burner](https://docs.m5stack.com/en/download) or esptool:

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 pomosense.bin
```

### Build from source

1. Install [PlatformIO](https://platformio.org/)
2. Clone this repository
3. Build and upload:

```bash
pio run -e m5stack-cardputer -t upload
```

## Controls

| Key | Action |
|-----|--------|
| Q / 7 | Start 25min (UP) |
| W / 9 | Start 60min (DOWN) |
| E / 1 | Start 5min (LEFT) |
| R / 3 | Start 15min (RIGHT) |
| Space | Pause / Resume |
| Ctrl | Quit to idle screen |
| ` | Reset timer (detects current orientation) |
| B | Switch background (None → Rain → Matrix) |
| F | Toggle auto-continuous mode |
| T | Toggle help screen |
| Enter | WiFi setup (on idle screen without WiFi) |

## Timer Directions

| Orientation | Duration |
|------------|----------|
| UP | 25 min |
| DOWN | 60 min |
| LEFT | 5 min |
| RIGHT | 15 min |

All timers use unified Morandi colors: yellow for minutes, blue for seconds.

## WiFi Setup

1. On the idle screen, press **Enter** when "No WiFi" is shown
2. Type your SSID and press **Enter**
3. Type your password and press **Enter** to connect
4. Press **S** to toggle password visibility
5. Credentials are saved to NVS and auto-connect on next boot

## Credits

- Original [TomatoClock_M5Cardputer](https://github.com/ffrafat/TomatoClock_M5Cardputer) by [Faisal F Rafat](https://github.com/ffrafat)
- Enhanced version by [Li Chunlei](https://github.com/lichunlei)

## License

[MIT](LICENSE)

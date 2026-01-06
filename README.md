# 8x8 Crawler Controller

ESP32-S3 based controller for an 8x8 RC crawler with multi-axle steering support.

## Features

- **6-Channel RC Input** - Reads throttle, steering, and 4 aux channels
- **ESC Control** - Motor speed control, optional realistic throttle (coasting/drag brake)
- **4 Servo Outputs** - Independent steering for all 4 axles
- **Web Dashboard** - Real-time status via WiFi (phone/tablet/PC)
- **Multiple Steering Modes** (for 4-axle, 8-wheel vehicle):
  - Front steering (Axles 1-2, car-like)
  - Rear steering (Axles 3-4)
  - All-axle steering (tight turns)
  - Crab steering (sideways movement)
- **Automatic Calibration** - Learns your transmitter's range
- **Servo & ESC Tuning** - Endpoints, trim/subtrim, expo, throttle limits
- **OTA Updates** - Firmware updates via web interface
- **WiFi STA Mode** - Connect to existing WiFi network
- **Persistent Storage** - Calibration and tuning saved to flash (NVS)
- **RGB Status LED** - WS2812 LED with colorful effects
- **Engine Sound** - Realistic diesel engine sounds with multiple profiles
- **Horn** - Selectable horn sounds (Truck Horn, MAN KAT Horn)
- **UDP Logging** - Wireless debug logging over UDP

## Hardware Setup

### Pin Configuration

| Function | GPIO | Description |
| ----------- | ---- | -------------------------------- |
| RC Steering | 6 | Channel 1 input |
| RC Throttle | 5 | Channel 2 input |
| RC Aux1 | 4 | Channel 3 input (horn / menu confirm) |
| RC Aux2 | 3 | Channel 4 input (steering mode / menu) |
| RC Aux3 | 2 | Channel 5 input (throttle mode - 3-pos switch) |
| RC Aux4 | 1 | Channel 6 input (engine on/off) |
| ESC | 12 | Motor ESC signal output |
| Servo A1 | 8 | Axle 1 (front) servo |
| Servo A2 | 9 | Axle 2 servo |
| Servo A3 | 10 | Axle 3 servo |
| Servo A4 | 11 | Axle 4 (rear) servo |
| Status LED | 21 | RGB WS2812 LED |

### 8x8 Vehicle Layout

```
    FRONT
   ┌─────┐
   │  ▲  │
 ═╪═════╪═  Axle 1 (A1) - Front
   │     │
 ═╪═════╪═  Axle 2 (A2)
   │  ●  │  ← Motor/ESC
 ═╪═════╪═  Axle 3 (A3)
   │     │
 ═╪═════╪═  Axle 4 (A4) - Rear
   └─────┘
    REAR
```

- Each axle has one servo controlling both wheels on that axle
- Axles 1-2 steer together in front-steer mode
- Axles 3-4 steer together in rear-steer mode

## Web Dashboard

The ESP32 creates a WiFi access point for real-time monitoring and configuration:

| Setting   | Value                |
| --------- | -------------------- |
| WiFi SSID | `8x8-Crawler`        |
| Password  | `crawler88`          |
| URL       | `http://192.168.4.1` |

**To use:**

1. Connect your phone/tablet to `8x8-Crawler` WiFi
2. Open browser and go to `http://192.168.4.1`
3. See real-time RC inputs, ESC/servo status, and steering mode

The dashboard updates 10 times per second via WebSocket.

### Web Pages

- **Dashboard** - Real-time status, steering mode selection, RC inputs, servo outputs
- **Settings** - WiFi STA configuration, OTA firmware updates
- **Calibration** - Web-based RC transmitter calibration
- **Tuning** - Servo endpoints, trim/subtrim, steering geometry, ESC settings

## Building and Flashing

```bash
# Build
idf.py build

# Flash
idf.py -p COM[X] flash

# Monitor
idf.py -p COM[X] monitor

# All in one
idf.py -p COM[X] flash monitor
```

## Calibration

### Automatic Calibration Trigger

Hold both sticks to top-right corner at power-on to enter calibration mode.

### Calibration Process

1. **Center Position** - Hold all sticks at center (neutral)
2. **Endpoints** - Move all sticks to their full extent (all corners)
3. **Save** - Calibration automatically saves to flash

### Manual Calibration Start

If no calibration exists, the system automatically enters calibration mode on first boot.

## Operation

### Status LED (RGB)

The RGB WS2812 LED provides colorful status indication:

| State | Effect | Color |
| ----- | ------ | ------ |
| Boot | Rainbow cycle | Multi |
| Idle | Breathe | Cyan |
| Running | Beacon | Orange |
| Failsafe | Fast blink | Red |
| Calibrating | Breathe | Yellow |
| OTA Update | Pulse | Purple |
| WiFi Connected | Double blink | Blue |
| Error | Solid | Red |

### Serial Monitor Output

```
THR:+500 STR:-200 | ESC:1750 | Mode:0
```

- THR: Throttle value (-1000 to +1000)
- STR: Steering value (-1000 to +1000)
- ESC: Current ESC pulse width (μs)
- Mode: Current steering mode

## Steering Modes

### Mode Switching

**Option 1: Web UI** - Tap mode buttons in the dashboard. Click "Auto" to return to RC control.

**Option 2: AUX2 Momentary Button** - Configure CH4 as a momentary button on your transmitter:

| Press Pattern | Action |
| ------------- | ------ |
| Single press | Toggle between Front and All-Axle steering |
| Double press | Switch to Crab mode |
| Triple press | Switch to Rear-only steering |
| Hold 1.5s | Enter settings menu |

**Returning from special modes:** When in Crab or Rear mode, a single press returns to the last active normal mode (Front or All-Axle).

**Startup:** The controller always starts in Front steering mode.

UI override takes priority. Click "Auto" in the web UI to let the momentary button control modes again.

### All Modes

| Mode         | A1-A2   | A3-A4   | Use Case        |
| ------------ | ------- | ------- | --------------- |
| 0 - Front    | Steer   | Fixed   | Normal driving  |
| 1 - Rear     | Fixed   | Steer   | Maneuvering     |
| 2 - All Axle | Steer → | ← Steer | Tight turns     |
| 3 - Crab     | Steer → | Steer → | Moving sideways |

### Mode Details

**Mode 0 - Front (Axles 1-2)**

```
     ↗ ↗      A1 - turns
     ↗ ↗      A2 - turns
     | |      A3 - straight
     | |      A4 - straight
```

Like a normal car. Front axles turn, rear axles follow straight.

**Mode 1 - Rear (Axles 3-4)**

```
     | |      A1 - straight
     | |      A2 - straight
     ↗ ↗      A3 - turns
     ↗ ↗      A4 - turns
```

Rear axles steer while front stays straight. Feels like driving in reverse.

**Mode 2 - All Axle (Counter-steer)**

```
     ↖ ↖      A1 - turns LEFT
     ↖ ↖      A2 - turns LEFT
     ↗ ↗      A3 - turns RIGHT (opposite)
     ↗ ↗      A4 - turns RIGHT (opposite)
```

Front and rear turn in opposite directions. Creates a very tight turning circle.

**Mode 3 - Crab**

```
     ↙ ↙      A1 - turns LEFT
     ↙ ↙      A2 - turns LEFT
     ↙ ↙      A3 - turns LEFT (same)
     ↙ ↙      A4 - turns LEFT (same)
```

All axles point the same direction. Vehicle moves sideways like a crab. Great for parallel parking.

## RC Controls

### Channel Assignments

| RC Channel | Control Type | Function |
| ---------- | ------------ | -------- |
| CH1 | Trigger | Throttle |
| CH2 | Wheel | Steering |
| CH3 (AUX1) | Momentary button | Horn (hold) / Menu confirm |
| CH4 (AUX2) | Momentary button | Steering mode + Menu entry |
| CH5 (AUX3) | 3-position switch | Throttle mode (Direct/Neutral/Realistic) |
| CH6 (AUX4) | Momentary button | Engine on/off |

### Settings Menu

Long-press AUX2 (1.5 seconds) to enter the settings menu. The menu has 2 levels:

**Level 1 - Categories:**
- Volume (1 beep)
- Sound Profile (2 beeps)
- WiFi (3 beeps)

**Level 2 - Options:**
- Volume: Low (50%), Medium (100%), High (150%)
- Profile: CAT 3408, Unimog, MAN TGX
- WiFi: On, Off

**Navigation:**
| Button | Level 1 | Level 2 |
| ------ | ------- | ------- |
| AUX2 press | Cycle categories | Cycle options |
| AUX1 press | Enter category | Confirm & exit |
| AUX2 hold | Exit menu | Back to Level 1 |
| 4s timeout | Exit menu | Exit menu |

## Horn

Press and hold **AUX1** to sound the horn. The horn plays continuously while the button is held.

**Note:** When in the settings menu, AUX1 is used for menu confirmation instead of horn.

### Horn Types

Three horn sounds are available, selectable in the web UI Sound Settings or via menu:

| Horn Type | Description |
| --------- | ----------- |
| Truck Horn | Classic truck air horn |
| MAN TGE Horn | MAN TGE truck horn |
| La Cucaracha | Musical horn melody |

The horn volume can be adjusted in the web UI Sound Settings page.

## Porting to Other ESP32 Variants

### Supported Targets

This project is configured for **ESP32-S3** but can be ported to other ESP32 variants:

| Target | Support | Notes |
| ------ | ------- | ----- |
| ESP32-S3 | Full | Default target, tested |
| ESP32 | Full | Change target and pins |
| ESP32-S2 | Full | Single-core, works fine |
| ESP32-C3 | Limited | Single MCPWM group, needs rework |

**Requirements:**
- **Flash**: 4MB minimum (for OTA dual partitions)
- **MCPWM**: 2 groups preferred (1 for RC input + ESC, 1 for servos)
- **RMT**: 1 channel for WS2812 LED
- **Cores**: Single-core works fine but dual core is recommended

### Changing Target

1. Set the target:
   ```bash
   idf.py set-target esp32    # or esp32s2, esp32s3, etc.
   ```

2. Update `sdkconfig.defaults`:
   ```
   CONFIG_IDF_TARGET="esp32"
   # Remove ESP32-S3 specific options like CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
   ```

3. Update pin definitions in `main/config.h`

### Pin Remapping

All pins are defined in [main/config.h](main/config.h). To change pins:

```c
// RC Receiver Input Pins
#define PIN_RC_STEERING     6   // Change to your GPIO
#define PIN_RC_THROTTLE     5
#define PIN_RC_AUX1         4
#define PIN_RC_AUX2         3
#define PIN_RC_AUX3         2
#define PIN_RC_AUX4         1

// ESC Output
#define PIN_ESC             12

// Servo Outputs
#define PIN_SERVO_AXLE_1    8
#define PIN_SERVO_AXLE_2    9
#define PIN_SERVO_AXLE_3    10
#define PIN_SERVO_AXLE_4    11

// Status LED
#define PIN_STATUS_LED      21
```
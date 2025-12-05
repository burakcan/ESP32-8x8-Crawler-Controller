# 8x8 Crawler Controller

ESP32-based controller for an 8x8 RC crawler with multi-axle steering support.

## Features

- **4-Channel RC Input** - Reads throttle, steering, and 2 aux channels
- **ESC Control** - Motor speed control with failsafe
- **4 Servo Outputs** - Independent steering for all 4 axles
- **Web Dashboard** - Real-time status via WiFi (phone/tablet/PC)
- **Multiple Steering Modes** (for 4-axle, 8-wheel vehicle):
  - Front steering (Axles 1-2, car-like)
  - Rear steering (Axles 3-4)
  - All-axle steering (tight turns)
  - Crab steering (sideways movement)
  - Spin mode (rotation in place)
- **Automatic Calibration** - Learns your transmitter's range
- **Persistent Storage** - Calibration saved to flash (NVS)
- **Failsafe Protection** - Safe stop on signal loss

## Hardware Setup

### Pin Configuration

| Function    | GPIO | Description                  |
| ----------- | ---- | ---------------------------- |
| RC Throttle | 34   | Channel 1 input (input-only) |
| RC Steering | 35   | Channel 2 input (input-only) |
| RC Aux1     | 32   | Cha nnel 3 input             |
| RC Aux2     | 33   | Channel 4 input              |
| ESC         | 18   | Motor ESC signal output      |
| Servo A1    | 19   | Axle 1 (front) servo         |
| Servo A2    | 21   | Axle 2 servo                 |
| Servo A3    | 22   | Axle 3 servo                 |
| Servo A4    | 23   | Axle 4 (rear) servo          |
| Status LED  | 2    | Built-in LED (DevKit)        |

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

### Wiring Notes

- All grounds must be connected together (ESP32, receiver, ESC, servos)
- RC receiver powered from 5V (BEC or external)
- Servos powered from separate BEC (5-6V, sufficient current)
- ESC powered from main battery

## Web Dashboard

The ESP32 creates a WiFi access point for real-time monitoring:

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

### Status LED

- **3 blinks** - System ready
- **Solid on** - Running normally
- **Blinking** - Calibration mode
- **Off** - Failsafe (signal lost)

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

**Option 1: Web UI** - Tap mode buttons in the dashboard. Click "Auto" to return to AUX control.

**Option 2: AUX channels** - Use transmitter toggle switches:

| AUX1 | AUX2 | Mode     | Description              |
| ---- | ---- | -------- | ------------------------ |
| OFF  | OFF  | Front    | Normal car-like steering |
| ON   | OFF  | All Axle | Tight turns              |
| OFF  | ON   | Crab     | Move sideways            |
| ON   | ON   | Rear     | Rear axles steer         |

UI override takes priority. Click "Auto" in the web UI to let AUX switches control modes again.

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
     ↙ ↘      A1 - turns
     ↙ ↘      A2 - turns
     | |      A3 - straight
     | |      A4 - straight
```

Like a normal car. Front axles turn, rear axles follow straight.

**Mode 1 - Rear (Axles 3-4)**

```
     | |      A1 - straight
     | |      A2 - straight
     ↖ ↗      A3 - turns
     ↖ ↗      A4 - turns
```

Rear axles steer while front stays straight. Feels like driving in reverse.

**Mode 2 - All Axle (Counter-steer)**

```
     ↙ ↘      A1 - turns LEFT
     ↙ ↘      A2 - turns LEFT
     ↗ ↖      A3 - turns RIGHT (opposite)
     ↗ ↖      A4 - turns RIGHT (opposite)
```

Front and rear turn in opposite directions. Creates a very tight turning circle - vehicle pivots around its center.

**Mode 3 - Crab**

```
     ↙ ↙      A1 - turns LEFT
     ↙ ↙      A2 - turns LEFT
     ↙ ↙      A3 - turns LEFT (same)
     ↙ ↙      A4 - turns LEFT (same)
```

All axles point the same direction. Vehicle moves sideways like a crab. Great for parallel parking.

**Mode 4 - Spin**

```
     ↙ ↘      A1 - full turn
     ↙ ↘      A2 - full turn
     ↗ ↖      A3 - full opposite
     ↗ ↖      A4 - full opposite
```

Same geometry as All Axle mode. With throttle, vehicle rotates in place like a tank.

## Project Structure

```
├── main/
│   ├── main.c           # Application entry and control loop
│   ├── config.h         # Pin definitions and constants
│   ├── nvs_storage.c/h  # Persistent storage for calibration
│   ├── rc_input.c/h     # RC receiver signal capture
│   ├── pwm_output.c/h   # ESC and servo PWM output
│   ├── calibration.c/h  # Calibration system
│   ├── web_server.c/h   # WiFi AP and HTTP/WebSocket server
│   └── CMakeLists.txt   # Component build config
├── web/
│   ├── index.html       # Dashboard HTML
│   ├── style.css        # Styling
│   └── app.js           # WebSocket client
├── partitions.csv       # Custom partition table (NVS + SPIFFS)
└── CMakeLists.txt       # Project build config
```

## Future Features

- [ ] Mode switching via aux channel
- [ ] Expo/rates adjustment
- [ ] Throttle limiting
- [ ] Differential steering for tank mode
- [ ] Telemetry output

## Troubleshooting

- **No signal**: Check RC receiver power and binding
- **ESC not arming**: Calibrate ESC to match controller output range
- **Erratic steering**: Run calibration, check servo connections
- **Calibration fails**: Ensure full stick movement during endpoint phase

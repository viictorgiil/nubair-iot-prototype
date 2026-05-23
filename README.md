# NUBAIR – IoT Prototype: Occupancy Monitoring & Door Control

> **Technical collaboration** for a seed-stage startup developing private rest modules for airports.  
> My scope: IoT firmware, server infrastructure, and physical prototype.

---

## Overview

NUBAIR cabins are modular rest pods designed to be installed in airports. This repository contains the full technical implementation of the IoT MVP prototype built to validate the concept:

- A physical **1:12 scale prototype** 3D-printed in modular sections
- An **ESP32** managing all sensors and actuators
- A **Raspberry Pi 4B** running Mosquitto (MQTT broker) and Home Assistant in Docker
- A **Home Assistant dashboard** showing the cabin image with an interactive door button

```
ESP32  <──WiFi/MQTT──>  Mosquitto  <──MQTT──>  Home Assistant
```

---

## My Responsibilities

- **3D CAD design** of the physical cabin prototype (OnShape, modular structure, 3D printed)
- **Server setup** on Raspberry Pi 4B (OS, SSH, static IP, Docker)
- **Mosquitto + Home Assistant** deployment via Docker Compose
- **ESP32 firmware** — sensors, servo, FSM logic, MQTT integration
- **Home Assistant dashboard** — cabin image with interactive door icon overlay
- **Full system integration testing**

---

## Repository Structure

```
nubair-iot-prototype/
│
├── firmware/
│   ├── nubair_proto_standalone/      # Standalone: all hardware, no WiFi/MQTT
│   │   └── nubair_proto_standalone.ino
│   │
│   └── nubair_proto_mqtt/            # Full version: WiFi + MQTT + Home Assistant
│       ├── nubair_proto_mqtt.ino
│       └── secrets.h.example         # Copy to secrets.h and fill in your values
│
├── server/
│   ├── docker-compose.yml            # Mosquitto + Home Assistant
│   └── mosquitto/
│       └── config/
│           └── mosquitto.conf
│
├── docs/
│   └── images/                       # Diagrams, screenshots, prototype photos
│
├── cad/
│   └── stl/                          # STL files to print
└── .gitignore
```

---

## Hardware

| Component | Model | Role |
|---|---|---|
| Microcontroller | ESP32 (38-pin) | Main controller |
| Servo | Standard 5V servo | Door mechanism |
| Touch sensor | TTP223 | Door open trigger (inside cabin) |
| Ultrasonic sensor | HC-SR04 | Presence detection |
| Display | SSD1306 OLED 128×64 | Status display (I2C) |
| Buzzer | Passive buzzer | Audio feedback |
| Server | Raspberry Pi 4B | MQTT broker + Home Assistant |

### Pin Map

| Pin | Component |
|---|---|
| GPIO 13 | Servo signal |
| GPIO 27 | Touch sensor (TTP223) |
| GPIO 5  | HC-SR04 TRIG |
| GPIO 18 | HC-SR04 ECHO |
| GPIO 25 | Buzzer |
| GPIO 21 | OLED SDA (I2C) |
| GPIO 22 | OLED SCL (I2C) |

---

## Firmware

### Phase 1 — Standalone (`nubair_proto_standalone`)

Used to validate all hardware before introducing network complexity.

**State machine logic:**

- OLED shows real-time distance readings from the ultrasonic sensor
- Holding the touch sensor for **3 seconds** triggers `openDoor()`
- Door stays open for **5 seconds**, then auto-closes
- Buzzer plays a melody on open, a short beep on close

### Phase 2 — MQTT (`nubair_proto_mqtt`)

Adds WiFi connectivity and full MQTT integration.

**MQTT topics:**

| Topic | Direction | Payload |
|---|---|---|
| `esp32/status` | ESP32 → broker | `online` / `offline` (LWT) |
| `esp32/door/state` | ESP32 → broker | `OPEN` / `CLOSED` |
| `esp32/door/set` | broker → ESP32 | `OPEN` / `CLOSE` |
| `esp32/distance/state` | ESP32 → broker | distance in cm |

**Setup:**

1. Copy `secrets.h.example` → `secrets.h`
2. Fill in your WiFi SSID, password, and Raspberry Pi IP
3. Flash to ESP32 via Arduino IDE

> **Note:** The ESP32 must connect to a **2.4 GHz** WiFi network. 5 GHz is not supported by the ESP32.

**Libraries required (Arduino IDE Library Manager):**

- `Adafruit GFX Library`
- `Adafruit SSD1306`
- `ESP32Servo`
- `PubSubClient`

---

## Server Setup (Raspberry Pi 4B)

### Prerequisites

- Raspberry Pi OS Lite 64-bit
- Docker installed (see below)
- Static IP configured via NetworkManager

### Deploy

```bash
# Clone the repo on the Raspberry Pi
git clone https://github.com/YOUR_USERNAME/nubair-iot-prototype.git
cd nubair-iot-prototype/server

# Create required directories
mkdir -p mosquitto/{data,log}

# Start services
docker compose up -d

# Verify
docker ps
```

Mosquitto will be available on port `1883`. Home Assistant on port `8123`.

### Install Docker (Raspberry Pi)

```bash
sudo install -m 0755 -d /etc/apt/keyrings
curl -fsSL https://download.docker.com/linux/debian/gpg | sudo tee /etc/apt/keyrings/docker.asc > /dev/null
sudo chmod a+r /etc/apt/keyrings/docker.asc

echo \
  "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/debian \
  $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
  sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

sudo usermod -aG docker $USER
```

### MQTT Discovery for Home Assistant

Run this once to register the door switch entity in Home Assistant automatically:

```bash
docker exec -i nubair-mosquitto mosquitto_pub \
  -h localhost \
  -t "homeassistant/switch/nubair_door/config" \
  -r \
  -m '{
    "name": "Puerta Nubair",
    "unique_id": "nubair_esp32_door",
    "command_topic": "esp32/door/set",
    "state_topic": "esp32/door/state",
    "payload_on": "OPEN",
    "payload_off": "CLOSE",
    "state_on": "OPEN",
    "state_off": "CLOSED",
    "availability_topic": "esp32/status",
    "payload_available": "online",
    "payload_not_available": "offline",
    "device": {
      "identifiers": ["nubair_esp32"],
      "name": "Nubair ESP32",
      "manufacturer": "Nubair",
      "model": "ESP32 Door Controller"
    }
  }'
```

### Home Assistant Dashboard Card

The dashboard uses a `picture-elements` card with the cabin image as background and an interactive icon positioned over the door:

```yaml
type: picture-elements
elements:
  - type: state-icon
    entity: switch.nubair_esp32_puerta_nubair
    icon: mdi:door
    tap_action:
      action: toggle
    hold_action:
      action: none
    style:
      top: 75%
      left: 15%
      transform: translate(-50%, -50%) scale(2)
      "--state-icon-color": black
image: /local/cabina.png
```

---

## Testing

### Test MQTT from the Raspberry Pi

```bash
# Subscribe to all ESP32 topics
docker exec -it nubair-mosquitto mosquitto_sub -h localhost -t "esp32/#" -v

# Manually open the door
docker exec -it nubair-mosquitto mosquitto_pub -h localhost -t "esp32/door/set" -m "OPEN"

# Manually close the door
docker exec -it nubair-mosquitto mosquitto_pub -h localhost -t "esp32/door/set" -m "CLOSE"
```

### Check Mosquitto logs

```bash
docker logs nubair-mosquitto --tail=30
```

---

## Physical Prototype

The cabin enclosure was designed in **OnShape** as a modular assembly and 3D-printed in PLA.

The electronics were installed inside the prototype to validate:
- Sensor placement and wiring
- Servo actuation of the door mechanism
- OLED visibility from inside the cabin

> **Known limitations of current prototype:** The ultrasonic sensor and servo mounting are provisional. The servo is not yet mechanically coupled to the door in a way that produces reliable actuation. These are hardware integration tasks pending for the next iteration.

*Photos and CAD exports in `docs/images/`.*

---

## Security Notes

The current Mosquitto configuration uses `allow_anonymous true` for ease of development. Before any production or public deployment:

1. Disable anonymous access in `mosquitto.conf`:
```conf
allow_anonymous false
password_file /mosquitto/config/passwordfile
```

2. Create MQTT users:
```bash
docker exec -it nubair-mosquitto mosquitto_passwd -c /mosquitto/config/passwordfile homeassistant
docker exec -it nubair-mosquitto mosquitto_passwd /mosquitto/config/passwordfile esp32
```

3. Update Home Assistant MQTT integration and ESP32 firmware accordingly.

---

## Roadmap

> This prototype represents a **Beta phase** focused on validating the core concept. The following work is pending for the next iteration.

### Hardware

- Proper mechanical coupling of the servo to the door mechanism
- Fixed mounting of the ultrasonic sensor at the correct angle and distance
- Clean wiring and cable management inside the cabin enclosure
- Improve the 3D Design by making the joints more robust

### Firmware & Logic

- Automatic occupancy state management based on sensor readings, removing the need for manual MQTT commands
- Finite state machine extended with states: `AVAILABLE`, `OCCUPIED`, `DOOR_OPENING`, `DOOR_CLOSING`

### Backend

- Lightweight database (SQLite or InfluxDB) to log occupancy events
- Records: user identifier, entry timestamp, exit timestamp, session duration
- REST API or MQTT-to-DB bridge running on the Raspberry Pi

### Dashboard

- Occupancy history view in Home Assistant or a dedicated web interface
- Real-time session timer visible while the cabin is in use

## Stack

`C++` · `ESP32` · `Arduino` · `MQTT` · `Mosquitto` · `Docker` · `Home Assistant` · `Raspberry Pi` · `I2C` · `OnShape (CAD)`

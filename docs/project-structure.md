# SmartCoaster Project Guide

## What this project does

SmartCoaster is an embedded-system project for a drink coaster that helps a
person keep track of their drinking. One ESP32 sits in the coaster and measures
the drink's weight and the distance to the cup. A second ESP32 acts as a
gateway: it receives those measurements, reads conditions in the room, and
sends the collected data to Firebase. A web dashboard displays the latest
measurements and recent history.

The gateway also detects drinks from the weight changes, runs a small
logistic-regression model that estimates the risk of under-drinking, lifts a
reminder sign with a servo, and sends a LINE alert when the sign is ignored.
See `docs/ai-and-drinks.md`.

## How data moves through the system

```text
Load cell + HX711 ─┐
Ultrasonic sensor ─┴─> Node ESP32
                         │
                         │ ESP-NOW
                         v
Room sensors ───────> Gateway ESP32 ──Wi-Fi/HTTPS──> Firebase Realtime Database
                                                        │
                                                        v
                                                   Web dashboard

Gateway ESP32 ──> Servo and reminder sign
                  (raised by the AI model, lowered after a detected drink)
```

The node sends a measurement packet about every five seconds. The gateway
combines it with its own sensor readings and writes data to:

- `/readings/<auto-id>` for the history of received readings.
- `/latest` for the newest reading, which the dashboard uses for its summary
  cards.

In other words, the node is the **measurement device**, the gateway is the
**collector and internet connection**, Firebase is the **cloud data store**, and
the dashboard is the **viewer**. ESP-NOW carries data between the two nearby
ESP32 boards; Wi-Fi carries data from the gateway to Firebase.

## What each board does

### Node: the coaster

The node is the ESP32 connected to the coaster's sensors. Its main tasks are:

1. Start the HX711 load-cell amplifier and use the configured calibration
   factor to convert sensor readings into grams.
2. Set the scale to zero (tare) during startup. The coaster should be empty at
   this point.
3. Trigger the HC-SR04 ultrasonic sensor and measure the echo time to estimate
   the distance to the cup. A timeout is represented as `-1`.
4. Put the measurements, node ID, and increasing sequence number into a
   `NodeMsg` packet.
5. Send the packet to the gateway using ESP-NOW approximately every five
   seconds.

The sequence number helps identify packet order and gaps. The node currently
sends `batt_mv = 0`, so battery voltage is not yet measured by the firmware.
The configurable gateway MAC address and Wi-Fi channel must match the actual
gateway.

### Gateway: the sign box

The gateway is the ESP32 connected to the room sensors and sign hardware. It:

1. Joins a 2.4 GHz Wi-Fi network and prints its channel and IP address to Serial.
2. Listens for ESP-NOW packets from the coaster node.
3. Reads the DHT11 temperature/humidity and the KY-038 analog sound level
   periodically. The firmware measures sound as a short-window peak-to-peak
   ADC range; it is not a calibrated sound-pressure measurement.
4. Counts rising-edge events from the PIR motion sensor.
5. Adds the received packet's RSSI (radio signal strength) and the most recent
   room-sensor values to a Firebase record.
6. Tries to reconnect to Wi-Fi if the connection is lost.

The AI task raises the sign when the predicted risk is high and lowers it after
a drink. The emergency-stop button disables servo pulses until `c` is typed in
the Serial Monitor.

### Sensors and outputs at a glance

| Part | Connected to | What it contributes |
|---|---|---|
| Load cell + HX711 | Node | Weight on the coaster in grams; useful for tracking changes in the drink/cup weight. |
| HC-SR04 ultrasonic sensor | Node | Distance reading in centimeters; `-1` means no echo was received before timeout. |
| DHT11 | Gateway | Room temperature in °C and relative humidity in percent. |
| KY-038 sound sensor | Gateway | Relative analog peak-to-peak reading, not calibrated dB. |
| HC-SR501 PIR | Gateway | Cumulative count of detected motion events since startup. |
| SG90 servo | Gateway | Raises/lowers the reminder sign, driven by the AI decision. |
| Emergency-stop button | Gateway | Stops servo pulses after the button is pressed; a restart/reset is needed to clear the current firmware's stop state. |
| Battery divider | Node hardware plan | Intended to measure battery voltage; the present node sketch does not read it. |

Use `docs/wiring/pin-table.md` for exact pin connections, voltage requirements,
and power details. In particular, the HC-SR04 Echo pin can output 5 V and needs
the documented voltage divider when using the 5 V sensor version.

## Packet and Firebase data

`common/msg.h` defines the binary packet sent from node to gateway. It contains:

| Field | Meaning |
|---|---|
| `version` | Packet format version, so firmware can identify a protocol revision. |
| `node_id` | Identifier for the sending coaster node. |
| `seq` | Increasing packet sequence number for that node. |
| `weight_g` | Load-cell weight in grams. |
| `dist_cm` | Ultrasonic distance in centimeters, or `-1` for no echo. |
| `batt_mv` | Battery voltage in millivolts; currently `0` because it is not measured. |

After receiving a packet, the gateway creates a JSON record. In addition to
the packet fields, it includes:

| Firebase field | Source / meaning |
|---|---|
| `rssi` | Received signal strength for the ESP-NOW packet, in dBm. |
| `room_t`, `room_rh` | Latest valid temperature and humidity readings from the DHT11. |
| `sound_p2p` | Latest sound sensor peak-to-peak ADC range. |
| `motion_count` | Running PIR event count since gateway startup. |
| `ts` | Firebase server timestamp, stored in milliseconds. |

The gateway appends each record under `/readings` and writes the same record to
`/latest`. The dashboard listens for updates to `/latest` and to the last 100
records in `/readings`. This is a monitoring display: it does not currently
send commands back to the boards.

## Following one reading end to end

1. Put the cup on the coaster. The load cell changes its electrical signal as
   the load changes; HX711 amplifies and digitizes that signal.
2. The node converts the HX711 value using `CAL_FACTOR` and reads the distance
   sensor. It increments `seq` and sends the packet.
3. The gateway's ESP-NOW receive callback copies the packet and records its
   radio RSSI. The main loop processes the received data.
4. The gateway adds its latest room readings, PIR count, and timestamp, then
   sends an HTTPS request to Firebase.
5. Firebase saves the reading in `/readings` and replaces `/latest`.
6. The open dashboard page receives the database update and refreshes the
   summary cards and charts.

If a value does not appear, troubleshoot in this order: confirm the sensor with
its individual test sketch, confirm the node's Serial output, confirm the
gateway's `RX node=...` output, check the gateway's Firebase HTTP response, and
then check the browser's Firebase connection/status message.

## Repository layout

```text
SmartCoaster/
├── README.md
├── platformio.ini
├── src/
│   └── main.cpp
├── firebase.json
├── .firebaserc.example
├── common/
│   ├── msg.h
│   └── line_push.h
├── node/
│   ├── node.ino
│   └── msg.h
├── gateway/
│   ├── gateway.ino
│   ├── msg.h
│   ├── line_push.h
│   └── secrets.example.h
├── tests/
│   ├── t01_blink/
│   ├── t02_dht11/
│   ├── t03_pir/
│   ├── t04_sound/
│   ├── t05_servo/
│   ├── t06_ultrasonic/
│   ├── t07_hx711_cal/
│   ├── t08_hx711_weigh/
│   └── t10_line/
├── dashboard/
│   ├── index.html
│   └── config.example.js
├── firebase/
│   ├── database.rules.json
│   └── database.rules.test-mode.json
└── docs/
    ├── block-diagram.png
    ├── calibration.md
    ├── line_setup.md
    └── wiring/
        ├── pin-table.md
        ├── wiring-node.png
        ├── wiring-gateway.png
        ├── gen.py
        └── tests/
            ├── gen_tests.py
            └── sensor test wiring diagrams
```

### Top-level files

- **`README.md`** — Main setup guide: required tools and libraries, sensor
  testing sequence, firmware configuration, Firebase setup, and dashboard
  launch instructions.
- **`platformio.ini`** — PlatformIO board and framework settings. The firmware
  sketches are kept in the separate `node/`, `gateway/`, and `tests/`
  directories. It defines separate build environments for the node, gateway,
  and each test sketch; see the PlatformIO instructions in `README.md`.
- **`src/main.cpp`** — PlatformIO entry point. The selected environment chooses
  one existing `.ino` sketch to compile, so the Arduino sketches remain the
  source of truth instead of being copied into a second implementation.
- **`firebase.json`** — Firebase CLI configuration. It points database rules
  to `firebase/database.rules.json` and uses `dashboard/` as the Hosting
  directory.
- **`.firebaserc.example`** — Example Firebase project selection file. Copy it
  to `.firebaserc` and set your own project ID when deploying with the Firebase
  CLI.
- **`.gitignore`** — Excludes local credentials/configuration and generated
  build/editor files from Git.

### Firmware and communication

- **`node/`** — Firmware for the coaster ESP32. `node.ino` reads the HX711 load
  cell and ultrasonic sensor, then sends a `NodeMsg` packet over ESP-NOW.
  `msg.h` is a local copy of the message definition needed by this sketch.
- **`gateway/`** — Firmware for the gateway ESP32. `gateway.ino` receives node
  packets, reads the DHT11, sound sensor, and PIR, and uploads readings to
  Firebase. It also initializes the servo and emergency-stop button. A manual
  Serial command (`m`) broadcasts a mock LINE reminder to all friends of the
  bot; automatic drink detection and reminder decisions are not implemented yet.
  `secrets.example.h` shows which Wi-Fi, Firebase, and LINE settings must be
  provided locally.
- **`common/msg.h`** — The canonical definition of the packet shared by node
  and gateway. The copies at `node/msg.h` and `gateway/msg.h` must stay
  identical. When changing the packet, update this file and copy it to both
  firmware folders.
- **`common/line_push.h`** — Canonical LINE Messaging API helper, including
  broadcast, single-user push, and monthly quota lookup. The local copies in
  `gateway/` and `tests/t10_line/` are kept for Arduino IDE sketches.

### Hardware tests

Each folder under **`tests/`** contains a standalone sketch for checking one
component before running the full system:

- `t01_blink` — Verifies the board starts, blinks its built-in LED, and prints
  its Wi-Fi station MAC address. Use the gateway board's MAC in the node
  configuration.
- `t02_dht11` — Prints temperature and humidity from the gateway's DHT11.
- `t03_pir` — Checks the gateway's motion sensor and event detection.
- `t04_sound` — Checks that the gateway can read the analog sound sensor.
- `t05_servo` — Moves the gateway servo between two positions and tests the
  emergency-stop button.
- `t06_ultrasonic` — Prints repeated distance readings from the node sensor;
  compare them to a ruler.
- `t07_hx711_cal` — Reads raw load-cell values and calculates a calibration
  factor using a known weight. Repeat with multiple known weights.
- `t08_hx711_weigh` — Applies a calibration factor, tares the empty scale, and
  prints weight readings. Send `t` over Serial to tare again.
- `t10_line` — Tests LINE broadcast messages from the gateway. Configure
  `tests/t10_line/secrets.h` from its example before building or uploading.

These are separate diagnostic sketches, not stages that run together. Upload
the test sketch for the board and sensor being checked, then upload the full
node or gateway firmware after testing.

### Dashboard and Firebase

- **`dashboard/index.html`** — Browser dashboard. It reads `/latest` for the
  summary cards and the last 100 `/readings` entries for charts.
- **`dashboard/config.example.js`** — Example Firebase web-app configuration.
  Copy it to `config.js` and fill in the values from your Firebase project.
- **`firebase/database.rules.json`** — Intended authenticated rules for
  deployment. Replace the placeholder gateway UID with the real UID.
- **`firebase/database.rules.test-mode.json`** — Open rules for temporary
  testing only. Do not use these rules for a deployed or publicly accessible
  database.

### Documentation and wiring

- **`docs/block-diagram.png`** — High-level view of the system.
- **`docs/wiring/pin-table.md`** — Pin assignments, power notes, and wiring
  guidance for both ESP32 boards.
- **`docs/wiring/wiring-node.png`** and **`docs/wiring/wiring-gateway.png`** —
  Full wiring diagrams for each board.
- **`docs/wiring/tests/`** — Wiring diagrams for individual sensor tests;
  `gen.py` and `gen_tests.py` generate the diagrams.
- **`docs/calibration.md`** — Record load-cell calibration values and compare
  ultrasonic readings with measured distances.

## Recommended bring-up sequence

This sequence helps isolate hardware, radio, cloud, and dashboard problems:

1. **Prepare the tools.** Follow `README.md` for Arduino IDE, ESP32 board
   support, libraries, and Serial Monitor settings. The project uses two ESP32
   DevKit V1 boards; label them `NODE` and `GATEWAY`.
2. **Check both boards.** Upload `tests/t01_blink/` to each board, verify the
   blink and record the gateway's station MAC address.
3. **Test each sensor separately.** Use the matching sketch under `tests/` and
   the corresponding wiring diagram. This confirms wiring before the full
   firmware adds more components.
4. **Calibrate the scale.** Use `t07_hx711_cal`, record several calibration
   results in `docs/calibration.md`, and use the agreed value in the node
   firmware. Verify it with `t08_hx711_weigh`.
5. **Configure and start the gateway.** Create the local
   `gateway/secrets.h` from its example, enter Wi-Fi and Firebase settings,
   upload the gateway sketch, and note the printed Wi-Fi channel.
6. **Configure and start the node.** Set the gateway MAC, Wi-Fi channel, and
   scale factor in `node/node.ino`, then upload it. The gateway Serial Monitor
   should show a received node packet about every five seconds.
7. **Configure Firebase and the dashboard.** Create the database, set rules
   appropriate for testing (never leave open test rules on a deployed
   database), create local `dashboard/config.js`, and serve the dashboard over
   HTTP as described in `README.md`.
8. **Verify the complete path.** Confirm a gateway receive message, a
   successful Firebase response, data under `/latest` and `/readings`, and
   updated dashboard cards/charts.

## Beginner's glossary

- **ESP32:** A microcontroller board that runs the firmware and connects to
  sensors, Wi-Fi, and ESP-NOW.
- **Firmware / sketch:** The program uploaded to a microcontroller. The `.ino`
  files in `node/`, `gateway/`, and `tests/` are sketches.
- **GPIO:** A numbered input/output pin on the ESP32 used to connect hardware.
- **HX711 / load cell:** The amplifier/converter and force sensor used together
  to estimate the weight placed on the coaster.
- **ESP-NOW:** A direct wireless communication method used here for packets
  between ESP32 boards; it does not send those packets through a router.
- **Wi-Fi / Firebase RTDB:** The gateway's network link and the cloud
  Realtime Database where readings are stored.
- **RSSI:** A measurement of received radio signal strength; a value closer to
  zero generally indicates a stronger signal.
- **Tare:** Set the scale's current reading as zero, normally with the empty
  coaster before placing a cup.
- **Calibration factor:** A value that maps the load cell's raw readings to
  physical units such as grams. It depends on the assembled sensor and should
  be measured, not copied blindly from an example.

## Important setup and safety notes

1. Test the sensors individually using the sketches in `tests/` before loading
   the full firmware.
2. Configure the gateway's Wi-Fi and Firebase values in a local
   `gateway/secrets.h`, and the dashboard's Firebase values in
   `dashboard/config.js`. These files are ignored by Git; do not commit real
   credentials or tokens.
3. Configure the node's gateway MAC address, Wi-Fi channel, and load-cell
   calibration factor in `node/node.ino`. The gateway reports its Wi-Fi channel
   over Serial.
4. The two boards both use GPIO18 for different devices: HX711 clock on the
   node and servo signal on the gateway. Keep the boards clearly identified.
5. The current gateway code uses insecure TLS (`setInsecure()`), and the
   example secrets leave Firebase authentication empty for test mode. These
   are development settings, not suitable for production; use authenticated
   database rules and certificate verification before deployment.

## Current implementation versus planned work

The current firmware implements coaster weight/distance readings, ESP-NOW
communication with application-level ACKs and a node retry queue, gateway room
sensing, FreeRTOS tasks and queues on the gateway, drink detection, the AI sign
decision, automatic LINE alerts, Firebase uploads with an offline flash queue,
certificate verification, Firebase Auth, and the dashboard. Node deep sleep and
battery measurement (`batt_mv` is sent as `0`) remain planned work.

Note: sections above that describe the packet layout and the single-loop
gateway predate message format v2 (`common/msg.h`) and the FreeRTOS gateway.

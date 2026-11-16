# LINE notification setup

## 1. LINE side (once)
1. LINE Developers Console -> your provider -> the Messaging API channel of your Official Account.
2. **Messaging API** tab -> *Channel access token (long-lived)* -> Issue. Copy it.
3. **Basic settings** tab -> bottom -> *Your user ID* (starts with `U`). Copy it.
4. Scan the bot's QR code (Messaging API tab) with your own LINE and add it as a friend.
5. In LINE Official Account Manager, check your plan's free message quota (push messages count).

## 2. Test from your computer first (no ESP32)
```bash
curl -X POST https://api.line.me/v2/bot/message/push \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer <CHANNEL_ACCESS_TOKEN>" \
  -d '{"to":"<USER_ID>","messages":[{"type":"text","text":"hello from curl"}]}'
```
`{}` and a message on your phone = credentials are correct.
Errors: 401 = bad token, 400 = bad JSON / user ID, 403 = not a friend / no permission.

## 3. Test on the ESP32
1. Copy `tests/t10_line/secrets.example.h` to
   `tests/t10_line/secrets.h`; set the 2.4 GHz Wi-Fi name/password, channel
   access token, and recipient user ID.
2. Build and upload the `t10_line` PlatformIO environment to the gateway board.
3. Open Serial Monitor at 115200 baud. Enter `s` to send a message or `q` to
   read the monthly quota usage.

The test sketch reconnects to Wi-Fi if it drops. A successful push returns HTTP
200; other responses are printed for diagnosis. Sending uses the channel's
message quota.

## 4. Files
| File | Purpose |
|---|---|
| `common/line_push.h` | canonical `linePush()` + `lineConsumption()` |
| `tests/t10_line/line_push.h` | identical copy for the test sketch |
| `gateway/line_push.h` | identical copy for the gateway firmware |
| `tests/t10_line/t10_line.ino` | standalone test sketch |
| `tests/t10_line/secrets.example.h` | template -> copy to `secrets.h` |

`**/secrets.h` is already ignored by `.gitignore`; do not commit real
credentials.

### PlatformIO
```ini
[env:t10_line]
build_flags = -DSC_SKETCH_T10_LINE
lib_deps =
    bblanchon/ArduinoJson@^7
```

## 5. Gateway mock (until drink detection exists)
The gateway firmware has a manual mock trigger so the LINE path can be tested
without pretending that drink detection is implemented:

- Enter `m` in the gateway Serial Monitor to send a **real** mock reminder.
  This uses LINE quota; successful sends have a 30-minute cooldown.
- Enter `q` to query monthly LINE usage.
- Failed sends can be retried after 10 seconds. HTTP requests run from `loop()`,
  never from the ESP-NOW receive callback.
- The request blocks the main loop while waiting for the network response; this
  manual test is not a substitute for the planned background task/queue.

Automatic detection of a weight drop followed by a stable reading, storing
events under `/drinks`, and deciding when to remind are still TODOs. Replace
the manual `m` trigger with that decision logic when implemented.

## 6. Before deploying
Replace `client.setInsecure()` in `line_push.h` with `client.setCACert(<root CA>)`
(the LINE token can send messages as your bot).
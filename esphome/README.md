# ESPHome port

`chicken-coop.yaml` is a drop-in ESPHome replacement for the Mongoose OS firmware in the repo root.
Same hardware, same default pins (see the wiring legend in the top-level README), same thresholds.

```bash
cd esphome
cp secrets.yaml.example secrets.yaml   # fill in
esphome run chicken-coop.yaml
```

## What maps to what

| Mongoose OS | ESPHome |
|---|---|
| `Door.c` open/close/stop, motor time backstop | `cover: endstop` with `max_duration` (`time.door_motor_active_seconds`) |
| H-bridge, never both HIGH, LOW on boot | two `gpio` switches with `interlock` and `restore_mode: ALWAYS_OFF` |
| open/closed reed switches | two `binary_sensor`s (`INPUT_PULLUP`, inverted) that are also the cover's endstops |
| DHT22, light sensor on A0 | `dht`, `adc` (`raw: true`, so 0-1024 as before) |
| light-triggered open/close, `time.light_trigger_min_hours` | `on_raw_value` lambda on the luminosity sensor |
| `NorthDoor.ResetLightTrigger` RPC | "Reset Light Trigger" button |
| `NorthDoor.Open/Close` HTTP RPC | `web_server` REST: `curl -X POST http://<ip>/cover/north_door/open` |
| Homie + HA discovery | MQTT with HA discovery (default), or swap `mqtt:` for `api:` |
| OTA via `/update` | ESPHome OTA |

## Not ported
* **Homie** and the **Mongoose RPC** (`rpc` topic, `NorthDoor.*` methods): not needed with Home Assistant.
* The old `stuck`/`unknown` door states. The cover reports open/closed/opening/closing; the two reed
  binary sensors are still exposed if you want to detect a jam (neither on) or a wiring fault (both on).
* The `mos.yml` luminosity thresholds and timings are now `substitutions:` at the top of the YAML.

## Notes
* The old light-trigger limiter in `Door_transition` compared with `>` where `<` was meant, so it
  blocked triggers once the interval had passed. The port implements the documented behaviour: at
  most one light-triggered move per `light_trigger_min_hours`, and the first one after boot is allowed.
* GPIO0 (DHT22) is a boot-strapping pin; it worked on the old firmware, but ESPHome will log a warning.

# ESPHome port

`chicken-coop.yaml` is a drop-in ESPHome replacement for the Mongoose OS firmware in the repo root.
Same hardware, same default pins (see the wiring legend in the top-level README), same pins and behaviour apart from the changes listed below.

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
| light-triggered open/close | replaced by the sun schedule below (luminosity is still reported) |
| `NorthDoor.Open/Close` HTTP RPC | `web_server` REST: `curl -X POST http://<ip>/cover/north_door/open` |
| Homie + HA discovery | MQTT with HA discovery (default), or swap `mqtt:` for `api:` |
| OTA via `/update` | ESPHome OTA |

## Sun schedule
Opens at sunrise and closes at civil dusk (sun 6 degrees below the horizon, roughly 30 minutes after sunset),
using the `sun` and SNTP `time` components. It runs on the device, so it works without Home Assistant
or the broker. Set `latitude`/`longitude` in `secrets.yaml` (decimal degrees, west is negative), and
adjust `open_elevation`, `close_elevation` and `timezone` in the substitutions. The "Sun Schedule" switch
pauses it. It replaces the light-sensor
trigger; the luminosity sensor is still published but no longer controls the door. The clock is
synced after boot while WiFi is up, so nothing runs until then. After a reboot or power cut, the
first clock sync puts the door where it should be for the time of day (open when the sun is above
`open_elevation`, closed when below `close_elevation`), since a sunrise/sunset may have been missed.

## Manual rocker switch
A rocker wired straight to the H-bridge works without the firmware, but the ESP can't see it. The reed
switches still can: when the cover is idle, the reed switches update the cover state (open / closed / half
way), so Home Assistant stays correct after a manual move.

If the rocker is wired in parallel with the ESP's GPIO4/GPIO5 (rather than through a switch/relay stage),
the ESP pins (driven LOW) and the rocker (pulling HIGH) would fight. The old firmware had the same
arrangement, but check it, and add series resistors or diodes if so.

## Not ported
* **Homie** and the **Mongoose RPC** (`rpc` topic, `NorthDoor.*` methods): not needed with Home Assistant.
* The old `stuck`/`unknown` door states. The cover reports open/closed/opening/closing; the two reed
  binary sensors are still exposed if you want to detect a jam (neither on) or a wiring fault (both on).
* Light-based open/close and its `luminosity.*` / `time.light_trigger_min_hours` settings; the sun
  schedule replaces them. Pins, motor run time and schedule settings are `substitutions:` at the top of the YAML.

## Notes
* GPIO0 (DHT22) is a boot-strapping pin; it worked on the old firmware, but ESPHome will log a warning.

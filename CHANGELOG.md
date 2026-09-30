# Changelog

## Non publié

### Features
- Support for stoves running mainboard firmware 2.26 / 2.27 (e.g. INDUO): the link comes up, every stove value is read and settings are applied.
- The stove type is detected automatically at startup (firmware 2.29, 2.28 or 2.26 / 2.27), nothing to configure.
- Every stove value now has a meaningful name, aligned with the official Rika names: warnings (`statusWarning`), air flaps (`airFlaps`, `airFlapsTarget`), error counters, display versions, and more.
- `/api/state` now reports the warning code (`warning_code`), the air flap position and target in % (`air_flaps_percent`, `air_flaps_target_percent`), and whether a room sensor is connected (`room_sensor_connected`).

### Fixes
- Stoves without a RIKA room sensor no longer show 102.4 °C: the room temperature is reported as unavailable (`null` in the API, `--` on the web page).
- The link no longer restarts every minute while the stove is idle: the stove only sends changes, so the bridge now checks regularly that it still answers.
- Delay between two frames sent to the stove lowered to 150 ms, for faster reactions.

# Changelog

## Non publié

### Features
- Support for stoves running mainboard firmware 2.26 / 2.27 (e.g. INDUO): the link comes up and every stove value is read. On these stoves, on/off, mode, heating power and target temperature can be changed; the other settings (schedule, MultiAir, frost protection, offset) are read-only for now. Stoves on firmware 2.28 are detected but not supported yet.
- The stove type is detected automatically at startup (firmware 2.29, 2.28 or 2.26 / 2.27), nothing to configure.
- Every stove value now has a meaningful name, aligned with the official Rika names: warnings (`statusWarning`), air flaps (`airFlaps`, `airFlapsTarget`), error counters, display versions, and more.
- `/api/state` now reports the warning code (`warning_code`), the air flap position and target in % (`air_flaps_percent`, `air_flaps_target_percent`), and whether a room sensor is connected (`room_sensor_connected`).

### Fixes
- Stoves without a RIKA room sensor no longer show 102.4 °C: the room temperature is reported as unavailable (`null` in the API, `--` on the web page).
- The link no longer restarts every minute while the stove is idle: the stove only sends changes, so the bridge now checks regularly that it still answers.
- Default delay between two frames sent to the stove lowered from 600 ms to 150 ms, for faster reactions (still adjustable from 50 to 600 ms in the Logs tab; a value you already set is kept).

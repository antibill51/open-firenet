# Changelog

## Non publié

### Features
- MQTT support, optional and off by default, for home automation systems other than Home Assistant (Jeedom, openHAB, Node-RED, Domoticz...). The bridge publishes the stove state to your broker (one JSON message, and one topic per value) with its availability, and accepts the same commands as the REST API (on/off, mode, power, target temperature, MultiAir, schedule...). Set it up in the Network tab of the web page; see the MQTT section of the README for the topics. (#52)

### Fixes
- `/api/state`: the stove model, its firmware version and build are `null` until the stove has sent them. They used to default to a DOMO on firmware 2.29, which looked like real data when the bridge was not linked to the stove at all.

## v3.2.0 (2026-10-03)

### Features
- The web page now tells why there is no link with the stove: bridge not connected to the stove over USB (wrong port on the board, charge-only cable, stove off), or connected but the stove stays silent. `/api/state` reports the same under `usb` (`host_connected`, `rx_bytes`).
- Web page header: the Open-Firenet version is shown next to the title, and a single badge gives the stove model, its firmware version and the link state (e.g. "DOMO v2.29 connected"). The badge turns grey when the stove is not connected.

### Fixes
- Stoves on firmware 2.28 (e.g. LIVO): the link works again. v3.0.0 and v3.1.0 talked to these stoves in the wrong message format, so the stove never answered and showed UW29.
- Web page header on phones: the title and the badge are no longer split over two lines.
- Web page: while no data has been received from the stove, the page says "Waiting for the stove" and greys out the values and controls, instead of showing a DOMO in standby with zero values.

## v3.1.0 (2026-10-02)

### Features
- Stoves on firmware 2.26 / 2.27 (e.g. INDUO): the heating schedule, frost protection, room sensor calibration and eco mode can now be changed too, not only on/off, mode, power and temperature.

### Fixes
- Web page, "Counters & maintenance" card: the two counters no longer overlap on wide screens, and their labels are clearer ("Running time: … h", then "Service in: … kg" right above its bar).
- Web page: the service bar is now a gauge of what is left before the next service, based on the interval reported by the stove (700 kg when it reports none). It turns orange below 20 %, and hovering it shows the figures.
- Web page, Settings tab: at most two cards per row; the eco mode card now sits on its own row below frost protection and room temperature calibration.
- Stoves on firmware 2.28 (e.g. LIVO): commands are sent in the short form again (on/off, mode, power, target temperature), as on 2.26 / 2.27. Since v3.0.0 they received the long form meant for firmware 2.29.

## v3.0.0 (2026-10-01)

### Features
- Support for stoves running mainboard firmware 2.26 / 2.27 (e.g. INDUO): the link comes up and every stove value is read. On these stoves, on/off, mode, heating power and target temperature can be changed; the other settings (schedule, MultiAir, frost protection, offset) are read-only for now. Stoves on firmware 2.28 are detected but not supported yet.
- The stove type is detected automatically at startup (firmware 2.29, 2.28 or 2.26 / 2.27), nothing to configure.
- Every stove value now has a meaningful name, aligned with the official Rika names: warnings (`statusWarning`), air flaps (`airFlaps`, `airFlapsTarget`), error counters, display versions, and more.
- Eco mode switch in the web page (Stove controls > Settings), enabled only when the stove reports that eco mode is possible.
- Eco mode can be read and changed: `eco_mode` / `eco_mode_possible` in `/api/state`, `ecoMode` in `/api/controls` (firmware 2.29; read-only on 2.26 / 2.27 for now). Settings now use their official Rika names too (`ecoMode` instead of `reserved6`, `debug0`…`debug4`).
- `/api/state` now reports the warning code (`warning_code`), the air flap position and target in % (`air_flaps_percent`, `air_flaps_target_percent`), and whether a room sensor is connected (`room_sensor_connected`).

### Breaking changes
- Removed the legacy endpoints `/api/status`, `/api/sensors` and `/api/arm`, and the duplicate routes `/restart` and `/reset-wifi`: use `/api/state`, `/api/restart` and `/api/forget` instead. The old `docs/api-explorer.html`, which relied on them, is removed too.
- The reply to `POST /api/controls` is now a short acknowledgement (`ok`, `on`, `mode`, `target_temperature`, `power_percent`); read `/api/state` for the full state.

### Fixes
- Smaller firmware (about 130 KB less flash, more room for future updates): the web page is stored compressed, and the command parsing of the API was rewritten around a single table.
- More free memory on the bridge: the debug log (CDC Logs tab, `/log`) keeps 48 KB of history, with repeated lines merged into one.
- Stoves without a RIKA room sensor no longer show 102.4 °C: the room temperature is reported as unavailable (`null` in the API, `--` on the web page).
- The link no longer restarts every minute while the stove is idle: the stove only sends changes, so the bridge now checks regularly that it still answers.
- Default delay between two frames sent to the stove lowered from 600 ms to 150 ms, for faster reactions (still adjustable from 50 to 600 ms in the Logs tab; a value you already set is kept).

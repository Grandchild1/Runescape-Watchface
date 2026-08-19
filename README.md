# RuneScape Watchface

A RuneScape-themed watchface for the **Pebble Time 2** (`emery` platform,
200x228 color display). Everything on screen — the stone-block background,
the gold interface frame, the minimap-style orbs, the weather icons and the
campfire — is drawn procedurally in C. No Jagex-owned art, fonts or text is
used; it's original vector artwork inspired by the game's classic look.

## Features

- **Time & date** in a gold-on-black parchment panel, styled after the
  game's chatbox/interface boxes.
- **Weather**, fetched via the phone (temperature + a hand-drawn icon for
  clear / cloudy / rain / snow / storm), refreshed every 30 minutes.
- **Animated campfire** (Firemaking-style) that flickers continuously at
  the bottom of the face.
- **Minimap orbs** in the corners, just like RuneScape's HUD:
  - Red **Hitpoints** orb = watch battery level
  - Blue **Prayer** orb = phone (Bluetooth) connection status
  - Green **Run energy** and yellow **Special attack** orbs are decorative

## Project layout

```
package.json        Pebble app manifest (UUID, target platform, message keys)
wscript              Build script (standard Pebble SDK boilerplate)
src/c/main.c         Watchface logic and all procedural drawing
src/pkjs/index.js    PebbleKit JS companion: fetches weather via geolocation
```

## Building

The original Pebble SDK is discontinued; the community-maintained
[Rebble](https://rebble.io) toolchain keeps it working. To build:

1. Install the Rebble `pebble-tool` (see the
   [Rebble developer docs](https://help.rebble.io/) for your OS).
2. From the project root:
   ```
   pebble build
   ```
3. Install to a phone running the Rebble app, or a connected emulator:
   ```
   pebble install --phone <phone-ip>
   # or
   pebble install --emulator emery
   ```

You can also open this folder as a project in
[CloudPebble](https://cloudpebble.rebble.io/), which needs no local
toolchain.

## Weather

The phone-side JS (`src/pkjs/index.js`) uses the phone's GPS location and
queries [Open-Meteo](https://open-meteo.com/) — a free weather API that
needs **no API key**. The watch asks for a refresh on load and every 30
minutes; the last known reading is cached on-watch (`persist_*`) so it
still shows something immediately after a relaunch, before the phone
responds.

To switch from Celsius to Fahrenheit, change `temperature_unit=celsius` to
`temperature_unit=fahrenheit` in `fetchWeather()` in `index.js`, and update
the `°C` suffix in `update_weather_text()` in `main.c`.

## Customizing

- Colors are all named `GColor*` constants at the top of the relevant
  drawing functions in `main.c` — swap them for any of the 64 Pebble
  colors to re-theme (e.g. a different cape/skill color scheme).
- The campfire's flicker speed is `FIRE_ANIMATION_INTERVAL_MS` in
  `main.c` (lower = faster, more battery use).
- Layout (panel positions/sizes) is controlled by the `#define`d constants
  just below the color/state globals in `main.c`.

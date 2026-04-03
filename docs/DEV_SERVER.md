# Dev Server Workflow

The development SignalK server lives at `../signalk-server/`.

**Always use the local dev server for development.**

---

## Starting the Dev Server

### With sample data (recommended for UI development)

```sh
cd ../signalk-server
node bin/signalk-server --sample-nmea0183-data --sample-n2k-data --override-timestamps
```

`--sample-nmea0183-data` plays back `samples/plaka.log` (NMEA 0183) — provides
heading, true wind, depth, STW, and other instrument data.

`--sample-n2k-data` plays back `samples/aava-n2k.data` (NMEA 2000) — provides
GPS position, SOG, COG, and additional navigation data.

Using both flags together gives the widest path coverage for development.

`--override-timestamps` makes the replayed data appear current so SignalK
does not mark it as stale.

### Without sample data

```sh
cd ../signalk-server && npm start
# equivalent to: node bin/signalk-server
```

Use this when connecting to real instruments or when you only care about the
WebSocket connection itself and not specific path values.

---

## Server endpoints

| URL | Purpose |
|-----|---------|
| `http://localhost:3000` | Admin UI — configure data connections, plugins, inspect live data |
| `http://localhost:3000/signalk` | REST discovery endpoint |
| `http://localhost:3000/signalk/v1/api/` | REST API root |
| `ws://localhost:3000/signalk/v1/stream` | WebSocket stream |

Server config and state live in `~/.signalk/` — managed separately from this project.

---

## Verifying the Server is Running

```sh
curl http://localhost:3000/signalk
```

Expected:
```json
{"endpoints":{"v1":{"version":"2.19.0","signalk-http":"http://localhost:3000/signalk/v1/api/","signalk-ws":"ws://localhost:3000/signalk/v1/stream",...}}}
```

Spot-check a specific path value:
```sh
curl http://localhost:3000/signalk/v1/api/vessels/self/navigation/speedOverGround
curl http://localhost:3000/signalk/v1/api/vessels/self/environment/wind/speedApparent
```

---

## Connecting the ESP32 to the Dev Server

The ESP32 and the Mac must be on the same **2.4GHz** WiFi network (the ESP32-C6
coprocessor does not support 5GHz).

### Automatic discovery (mDNS) — preferred

The firmware discovers SignalK servers automatically via mDNS. The dev server
advertises `_signalk-ws._tcp` by default. Set WiFi credentials via the
**WiFi Settings app** on the device (credentials persist in NVS across reboots).

On first boot with clean NVS, open the WiFi Settings app from the launcher,
select your network, and enter the password. The device will find the
SignalK server automatically — no IP address needed.

Verify mDNS is working on the Mac:
```sh
dns-sd -B _signalk-ws._tcp
```

### If mDNS doesn't find the server

There is no fallback URI. The device relies entirely on mDNS. If the server
isn't found, the device stays disconnected and retries automatically via the
status timer (every 500ms). Check:
- Both devices on the same 2.4GHz network
- `dns-sd -B _signalk-ws._tcp` shows the server on the Mac
- SignalK server is running with default mDNS advertisement

### Connection sequence

On boot the launcher shows the connection progress:
1. Amber SK logo — "Connecting WiFi..."
2. Amber SK logo — "Searching for SignalK..." (mDNS query, up to 3s)
3. Green SK logo — "Connected"

The `?subscribe=none` query string tells the server not to push the default
subscription. The firmware sends its own explicit subscribe message after
connecting.

---

## Which Paths the Dev Server Provides

With both `--sample-nmea0183-data` and `--sample-n2k-data`, the server replays
two sample logs providing wide coverage. The NMEA 0183 data (plaka.log) adds
heading, true wind, depth, and STW. The N2K data (aava-n2k.data) adds GPS,
SOG, COG, and battery data (instance 1).

| Path | Source | Available |
|------|--------|-----------|
| `navigation.speedOverGround` | N2K | ✓ |
| `navigation.speedThroughWater` | NMEA 0183 | ✓ |
| `navigation.headingMagnetic` | — | ✗ (app falls back to headingTrue) |
| `navigation.headingTrue` | NMEA 0183 | ✓ |
| `navigation.courseOverGroundTrue` | N2K | ✓ |
| `environment.wind.speedApparent` | Both | ✓ |
| `environment.wind.angleApparent` | Both | ✓ |
| `environment.wind.speedTrue` | NMEA 0183 | ✓ (also derived client-side as fallback) |
| `environment.wind.angleTrueWater` | NMEA 0183 | ✓ (also derived client-side as fallback) |
| `environment.depth.belowTransducer` | Both | ✓ |
| `environment.depth.belowKeel` | NMEA 0183 | ✓ |
| `environment.water.temperature` | N2K | ✓ |
| `performance.velocityMadeGood` | NMEA 0183 | ✓ |
| `navigation.trip.log` | NMEA 0183 | ✓ |
| `electrical.batteries.1.voltage` | N2K | ✓ |
| `electrical.batteries.1.current` | N2K | ✓ |

With `--sample-n2k-data` alone, true wind, heading, VMG, trip log, and
depth below keel are missing. Always use both flags for development.

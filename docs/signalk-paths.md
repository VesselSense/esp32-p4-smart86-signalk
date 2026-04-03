# SignalK Paths — Reference

Example paths from a production SignalK server. For development use the local dev server
(see docs/DEV_SERVER.md). The dev server's N2K sample data covers the navigation and
environment paths but not electrical/solar/tank paths.

Total example paths: 142

## Priority Instrument Paths for Display

| Path | Unit | Conversion | Display |
|------|------|-----------|---------|
| `navigation.speedOverGround` | m/s | × 1.94384 | knots |
| `navigation.speedThroughWater` | m/s | × 1.94384 | knots |
| `navigation.headingMagnetic` | rad | × 57.2958 mod 360 | degrees |
| `navigation.courseOverGroundTrue` | rad | × 57.2958 mod 360 | degrees |
| `environment.wind.speedApparent` | m/s | × 1.94384 | knots |
| `environment.wind.angleApparent` | rad | × 57.2958 | degrees (neg=port) |
| `environment.depth.belowTransducer` | m | none | meters |
| `environment.water.temperature` | K | − 273.15 | °C |
| `navigation.position` | {lat,lon} | none | degrees |
| `electrical.batteries.288.capacity.stateOfCharge` | 0-1 | × 100 | % |
| `electrical.batteries.288.voltage` | V | none | volts |
| `electrical.batteries.288.current` | A | none | amps |
| `electrical.solar.289.panelPower` | W | none | watts |
| `tanks.freshWater.main.currentLevel` | 0-1 | × 100 | % |

## Sample Instrument Pages (Screens for Scroll)
1. **Navigation**: SOG + COG (2-up)
2. **Wind**: AWS + AWA (2-up)
3. **Depth & Water Temp** (2-up)
4. **Battery**: SOC + voltage + current (3-up)
5. **Position**: lat/lon

## WebSocket Subscribe Message
```json
{
  "context": "vessels.self",
  "subscribe": [
    {"path": "navigation.speedOverGround", "period": 1000},
    {"path": "navigation.headingMagnetic", "period": 1000},
    {"path": "navigation.courseOverGroundTrue", "period": 1000},
    {"path": "environment.wind.speedApparent", "period": 500},
    {"path": "environment.wind.angleApparent", "period": 500},
    {"path": "environment.depth.belowTransducer", "period": 2000},
    {"path": "environment.water.temperature", "period": 5000},
    {"path": "electrical.batteries.288.capacity.stateOfCharge", "period": 5000},
    {"path": "electrical.batteries.288.voltage", "period": 2000},
    {"path": "tanks.freshWater.main.currentLevel", "period": 10000}
  ]
}
```

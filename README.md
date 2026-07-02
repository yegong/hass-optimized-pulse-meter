# hass-optimized-pulse-meter

An ESPHome external component for pulse-based flow meters where the volume represented by each pulse is not constant.

The built-in ESPHome `pulse_meter` works very well when each pulse maps to a fixed quantity, for example `N pulses = 1 liter` or `N pulses = 1 kWh`. Some flow sensors, however, are specified by a frequency/flow equation with a non-zero offset, such as:

```text
F = a * Q - b
```

where:

- `F` is pulse frequency in Hz
- `Q` is flow rate, commonly in L/min
- `a` and `b` are sensor-specific calibration constants

For this class of sensor, a low-frequency pulse represents a different volume from a high-frequency pulse. A totalizer that only accumulates raw pulse count cannot apply the offset term correctly.

`optimized_pulse_meter` keeps the edge-timing behavior of ESPHome's `pulse_meter`, but lets you define:

- `flow_lambda`: converts instantaneous frequency to the main sensor value.
- `volume_lambda`: converts the time interval for one pulse into the volume increment for the total sensor.
- `max_time_delta`: clamps unusually long pulse intervals so a stale or low-frequency edge does not create an unrealistic volume increment.

Negative, NaN, and infinite lambda outputs are clamped to `0`.

## Status

This component is intended for DIY/home-automation use. Validate it against a known meter before using it for billing, safety-critical decisions, or compliance-sensitive applications.

## Installation

### Option A: Use directly from GitHub

```yaml
external_components:
  - source: github://yegong/hass-optimized-pulse-meter@main
    components:
      - optimized_pulse_meter
```

### Option B: Use as a local external component

Clone or unzip this repository next to your ESPHome YAML, then configure:

```yaml
external_components:
  - source:
      type: local
      path: .
    components:
      - optimized_pulse_meter
```

Expected repository layout:

```text
hass-optimized-pulse-meter/
  README.md
  LICENSE
  NOTICE
  components/
    optimized_pulse_meter/
      __init__.py
      sensor.py
      optimized_pulse_meter_sensor.cpp
      optimized_pulse_meter_sensor.h
      automation.h
```

## Basic example

The following example uses a generic calibration equation:

```text
F = 5 * Q - 3
```

This means:

```text
Q = (F + 3) / 5
```

If `Q` is in L/min and you want the main sensor to publish `m³/h`:

```text
m³/h = Q * 0.06
```

For the total sensor, the component calls `volume_lambda` once per pulse with:

```text
x = time_delta_s
```

For `F = aQ - b`, the per-pulse volume is:

```text
delta_L = (1 + b * time_delta_s) / (60 * a)
delta_m³ = delta_L / 1000
```

YAML:

```yaml
sensor:
  - platform: optimized_pulse_meter
    id: main_flow
    name: "Main Flow Rate"
    pin:
      number: GPIO6
      mode:
        input: true
        pullup: false
        pulldown: false

    internal_filter: 1ms
    timeout: 10s
    max_time_delta: 500ms

    unit_of_measurement: "m³/h"
    device_class: volume_flow_rate
    state_class: measurement
    accuracy_decimals: 4

    flow_lambda: |-
      // x = frequency_hz
      // F = 5Q - 3, Q = (F + 3) / 5, Q unit = L/min
      const float q_l_min = (x + 3.0f) / 5.0f;
      return q_l_min * 0.06f;  // m³/h

    volume_lambda: |-
      // x = time_delta_s for one pulse
      // delta_m3 = (1 + 3dt) / (5 * 60000)
      return (1.0f + 3.0f * x) / (5.0f * 60000.0f);

    total:
      id: main_water_total
      name: "Main Water Total"
      unit_of_measurement: "m³"
      device_class: water
      state_class: total_increasing
      accuracy_decimals: 5
```

## Another generic calibration example

For:

```text
F = 10 * Q - 5
```

Use:

```yaml
flow_lambda: |-
  // x = frequency_hz
  const float q_l_min = (x + 5.0f) / 10.0f;
  return q_l_min * 0.06f;  // m³/h

volume_lambda: |-
  // x = time_delta_s for one pulse
  return (1.0f + 5.0f * x) / (10.0f * 60000.0f);
```

## Configuration options

All standard ESPHome sensor options are supported for the main sensor and the optional `total` sensor.

| Option | Required | Default | Description |
|---|---:|---:|---|
| `pin` | yes | — | Input pin connected to the pulse output. |
| `internal_filter` | no | `13us` | Minimum accepted edge interval or pulse length, depending on filter mode. |
| `internal_filter_mode` | no | `EDGE` | `EDGE` filters rising edges; `PULSE` filters by pulse length. |
| `timeout` | no | `5min` | Publish `0` for the main sensor after no pulses are detected for this duration. |
| `max_time_delta` | no | `500ms` | Upper clamp for the per-pulse time interval passed to lambdas. |
| `flow_lambda` | no | — | `x = frequency_hz`; return the main sensor value. Without this, the main sensor behaves like `pulse_meter` and publishes pulses/minute. |
| `volume_lambda` | no | — | `x = time_delta_s`; return the total increment for one pulse. Without this, the total sensor behaves like `pulse_meter` and accumulates raw pulse count. |
| `total` | no | — | Optional total sensor. |

## Actions

The component provides an action compatible with the optimized total value:

```yaml
api:
  actions:
    - action: set_optimized_pulse_total
      variables:
        new_total: float
      then:
        - optimized_pulse_meter.set_total:
            id: main_flow
            value: !lambda "return new_total;"
```

## Notes on totals and persistence

This component keeps the total in RAM. It does not persist the total to ESP flash by itself. For long-term accounting, use Home Assistant Recorder/statistics/utility meters and treat an ESP reboot as a meter reset. The total sensor should use:

```yaml
state_class: total_increasing
```

## Why not just integrate the flow sensor?

An integration sensor can work, but it integrates published flow values. Depending on update rate, timeout, and flow changes around start/stop edges, it can lose edge-level timing information. This component computes total increments directly from pulse timing.

## License

This component is derived from ESPHome's `pulse_meter` component. Because ESPHome's C++ runtime code is GPLv3 and its Python code is MIT, this repository is released under GPL-3.0-only. See `LICENSE` and `NOTICE`.

GPL does not prohibit commercial use by itself. It does, however, require that distributed derivative works comply with GPL terms, including source availability and the same license for covered code.

/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Derived from ESPHome pulse_meter component.
 */

#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"

#include <functional>

namespace esphome::optimized_pulse_meter {

class OptimizedPulseMeterSensor final : public sensor::Sensor, public Component {
 public:
  enum InternalFilterMode {
    FILTER_EDGE = 0,
    FILTER_PULSE,
  };

  void set_pin(InternalGPIOPin *pin) { this->pin_ = pin; }
  void set_filter_us(uint32_t filter) { this->filter_us_ = filter; }
  void set_timeout_us(uint32_t timeout) { this->timeout_us_ = timeout; }
  void set_max_time_delta_us(uint32_t max_time_delta) { this->max_time_delta_us_ = max_time_delta; }
  void set_total_sensor(sensor::Sensor *sensor) { this->total_sensor_ = sensor; }
  void set_filter_mode(InternalFilterMode mode) { this->filter_mode_ = mode; }

  // flow_lambda: x = frequency_hz, return the value published by the main sensor.
  void set_flow_lambda(std::function<float(float)> &&lambda) { this->flow_lambda_ = std::move(lambda); }

  // volume_lambda: x = time_delta_s for one pulse, return delta volume for that pulse.
  void set_volume_lambda(std::function<float(float)> &&lambda) { this->volume_lambda_ = std::move(lambda); }

  void set_total_value(float value);

  void setup() override;
  void loop() override;
  void dump_config() override;

 protected:
  static void edge_intr(OptimizedPulseMeterSensor *sensor);
  static void pulse_intr(OptimizedPulseMeterSensor *sensor);

  float clamp_sensor_value_(float value) const;
  uint32_t clamp_time_delta_us_(uint32_t delta_us) const;
  void publish_total_(uint32_t count, float pulse_width_us);
  void publish_flow_(float pulse_width_us);

  InternalGPIOPin *pin_{nullptr};
  uint32_t filter_us_ = 0;
  uint32_t timeout_us_ = 1000000UL * 60UL * 5UL;
  uint32_t max_time_delta_us_ = 500000UL;
  sensor::Sensor *total_sensor_{nullptr};
  InternalFilterMode filter_mode_{FILTER_EDGE};
  std::function<float(float)> flow_lambda_{};
  std::function<float(float)> volume_lambda_{};

  // The total sensor stores either user-defined accumulated volume, or raw pulse count fallback.
  float total_value_ = 0.0f;

  // Variables used in the loop
  enum class MeterState { INITIAL, RUNNING, TIMED_OUT };
  MeterState meter_state_ = MeterState::INITIAL;
  bool peeked_edge_ = false;
  uint32_t last_processed_edge_us_ = 0;

  // This struct and variable are used to pass data between the ISR and loop.
  // The data from state_ is read and then count_ in state_ is reset in each loop.
  // This must be done while guarded by an InterruptLock. Use this variable to send data
  // from the ISR to the loop not the other way around (except for resetting count_).
  struct State {
    uint32_t last_detected_edge_us_ = 0;
    uint32_t last_rising_edge_us_ = 0;
    uint32_t count_ = 0;
  };
  volatile State state_{};

  // Only use the following variables in the ISR or while guarded by an InterruptLock
  ISRInternalGPIOPin isr_pin_;

  /// The last pin value seen
  bool last_pin_val_ = false;

  /// Filter state for edge mode
  struct EdgeState {
    uint32_t last_sent_edge_us_ = 0;
  };
  EdgeState edge_state_{};

  /// Filter state for pulse mode
  struct PulseState {
    uint32_t last_intr_ = 0;
    bool latched_ = false;
  };
  PulseState pulse_state_{};
};

}  // namespace esphome::optimized_pulse_meter

/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Derived from ESPHome pulse_meter component.
 */

#include "optimized_pulse_meter_sensor.h"

#include <cmath>
#include <cinttypes>

#include "esphome/core/log.h"

namespace esphome::optimized_pulse_meter {

static const char *const TAG = "optimized_pulse_meter";

float OptimizedPulseMeterSensor::clamp_sensor_value_(float value) const {
  if (!std::isfinite(value) || value < 0.0f) {
    return 0.0f;
  }
  return value;
}

uint32_t OptimizedPulseMeterSensor::clamp_time_delta_us_(uint32_t delta_us) const {
  if (delta_us == 0) {
    return 1;
  }
  if (this->max_time_delta_us_ > 0 && delta_us > this->max_time_delta_us_) {
    return this->max_time_delta_us_;
  }
  return delta_us;
}

void OptimizedPulseMeterSensor::set_total_value(float value) {
  this->total_value_ = this->clamp_sensor_value_(value);
  if (this->total_sensor_ != nullptr) {
    this->total_sensor_->publish_state(this->total_value_);
  }
}

void OptimizedPulseMeterSensor::setup() {
  this->pin_->setup();
  this->isr_pin_ = pin_->to_isr();

  // Set the pin value to the current value to avoid a false edge.
  this->last_pin_val_ = this->pin_->digital_read();

  // Set the last processed edge to now for the first timeout and first delta clamp.
  this->last_processed_edge_us_ = micros();
  this->state_.last_detected_edge_us_ = this->last_processed_edge_us_;
  this->state_.last_rising_edge_us_ = this->last_processed_edge_us_;

  if (this->filter_mode_ == FILTER_EDGE) {
    this->pin_->attach_interrupt(OptimizedPulseMeterSensor::edge_intr, this, gpio::INTERRUPT_RISING_EDGE);
  } else if (this->filter_mode_ == FILTER_PULSE) {
    // Set the pin value to the current value to avoid a false edge.
    this->pulse_state_.latched_ = this->last_pin_val_;
    this->pin_->attach_interrupt(OptimizedPulseMeterSensor::pulse_intr, this, gpio::INTERRUPT_ANY_EDGE);
  }

  if (this->total_sensor_ != nullptr) {
    this->total_sensor_->publish_state(this->total_value_);
  }
}

void OptimizedPulseMeterSensor::publish_total_(uint32_t count, float pulse_width_us) {
  if (this->total_sensor_ == nullptr || count == 0) {
    return;
  }

  if (this->volume_lambda_) {
    const uint32_t effective_width_us = this->clamp_time_delta_us_(static_cast<uint32_t>(pulse_width_us));
    const float time_delta_s = effective_width_us / 1000000.0f;
    const float delta_per_pulse = this->clamp_sensor_value_(this->volume_lambda_(time_delta_s));
    this->total_value_ += delta_per_pulse * count;
  } else {
    // Backward-compatible fallback: raw pulse count as float.
    this->total_value_ += count;
  }

  this->total_sensor_->publish_state(this->total_value_);
}

void OptimizedPulseMeterSensor::publish_flow_(float pulse_width_us) {
  const uint32_t effective_width_us = this->clamp_time_delta_us_(static_cast<uint32_t>(pulse_width_us));
  const float frequency_hz = 1000000.0f / static_cast<float>(effective_width_us);

  if (this->flow_lambda_) {
    this->publish_state(this->clamp_sensor_value_(this->flow_lambda_(frequency_hz)));
  } else {
    // Backward-compatible fallback: pulses/minute.
    this->publish_state(60.0f * frequency_hz);
  }
}

void OptimizedPulseMeterSensor::loop() {
  State state;
  {
    // Lock the interrupt so the interrupt code doesn't interfere with itself.
    InterruptLock lock;

    // Sometimes ESP devices miss interrupts if the edge rises or falls too slowly.
    // See https://github.com/espressif/arduino-esp32/issues/4172
    // If the edges are rising too slowly it also implies that the pulse rate is slow.
    // Therefore the update rate of the loop is likely fast enough to detect the edges.
    // When the main loop detects an edge that the ISR didn't it will run the ISR functions directly.
    bool current = this->pin_->digital_read();
    if (this->filter_mode_ == FILTER_EDGE && current && !this->last_pin_val_) {
      OptimizedPulseMeterSensor::edge_intr(this);
    } else if (this->filter_mode_ == FILTER_PULSE && current != this->last_pin_val_) {
      OptimizedPulseMeterSensor::pulse_intr(this);
    }
    this->last_pin_val_ = current;

    // Get the latest state from the ISR and reset the count in the ISR state.
    state.last_detected_edge_us_ = this->state_.last_detected_edge_us_;
    state.last_rising_edge_us_ = this->state_.last_rising_edge_us_;
    state.count_ = this->state_.count_;
    this->state_.count_ = 0;
  }

  const uint32_t now = micros();

  // If an edge was peeked, repay the debt.
  if (this->peeked_edge_ && state.count_ > 0) {
    this->peeked_edge_ = false;
    state.count_--;
  }

  // If there is an unprocessed edge, and filter_us_ has passed since, count this edge early.
  // Wait for the debt to be repaid before counting another unprocessed edge early.
  if (!this->peeked_edge_ && state.last_rising_edge_us_ != state.last_detected_edge_us_ &&
      now - state.last_rising_edge_us_ >= this->filter_us_) {
    this->peeked_edge_ = true;
    state.last_detected_edge_us_ = state.last_rising_edge_us_;
    state.count_++;
  }

  // Check if we detected a pulse this loop.
  if (state.count_ > 0) {
    const uint32_t delta_us = state.last_detected_edge_us_ - this->last_processed_edge_us_;
    const float pulse_width_us = delta_us / float(state.count_);

    ESP_LOGV(TAG, "New pulse, delta: %" PRIu32 " µs, count: %" PRIu32 ", width: %.5f µs", delta_us,
             state.count_, pulse_width_us);

    this->publish_total_(state.count_, pulse_width_us);

    // We need to detect at least two edges to have a valid pulse width.
    switch (this->meter_state_) {
      case MeterState::INITIAL:
      case MeterState::TIMED_OUT: {
        this->meter_state_ = MeterState::RUNNING;
      } break;
      case MeterState::RUNNING: {
        this->publish_flow_(pulse_width_us);
      } break;
    }

    this->last_processed_edge_us_ = state.last_detected_edge_us_;
  }
  // No detected edges this loop.
  else {
    const uint32_t time_since_valid_edge_us = now - this->last_processed_edge_us_;
    switch (this->meter_state_) {
      // Running and initial states can timeout.
      case MeterState::INITIAL:
      case MeterState::RUNNING: {
        if (time_since_valid_edge_us > this->timeout_us_) {
          this->meter_state_ = MeterState::TIMED_OUT;
          ESP_LOGD(TAG, "No pulse detected for %" PRIu32 "s, assuming 0", time_since_valid_edge_us / 1000000);
          this->publish_state(0.0f);
        }
      } break;
      default:
        break;
    }
  }
}

void OptimizedPulseMeterSensor::dump_config() {
  LOG_SENSOR("", "Optimized Pulse Meter", this);
  LOG_PIN(" Pin: ", this->pin_);
  if (this->filter_mode_ == FILTER_EDGE) {
    ESP_LOGCONFIG(TAG, " Filtering rising edges less than %" PRIu32 " µs apart", this->filter_us_);
  } else {
    ESP_LOGCONFIG(TAG, " Filtering pulses shorter than %" PRIu32 " µs", this->filter_us_);
  }
  ESP_LOGCONFIG(TAG, " Assuming 0 after not receiving a pulse for %" PRIu32 "s", this->timeout_us_ / 1000000);
  ESP_LOGCONFIG(TAG, " Clamping per-pulse time_delta to <= %" PRIu32 " µs", this->max_time_delta_us_);
  ESP_LOGCONFIG(TAG, " Flow lambda: %s", this->flow_lambda_ ? "yes" : "no");
  ESP_LOGCONFIG(TAG, " Volume lambda: %s", this->volume_lambda_ ? "yes" : "no");
}

void IRAM_ATTR OptimizedPulseMeterSensor::edge_intr(OptimizedPulseMeterSensor *sensor) {
  // This is an interrupt handler - we can't call any virtual method from this method.
  // Get the current time before we do anything else so the measurements are consistent.
  const uint32_t now = micros();
  auto &edge_state = sensor->edge_state_;
  auto &state = sensor->state_;

  if ((now - edge_state.last_sent_edge_us_) >= sensor->filter_us_) {
    edge_state.last_sent_edge_us_ = now;
    state.last_detected_edge_us_ = now;
    state.last_rising_edge_us_ = now;
    state.count_ += 1;
  }

  // This ISR is bound to rising edges, so the pin is high.
  sensor->last_pin_val_ = true;
}

void IRAM_ATTR OptimizedPulseMeterSensor::pulse_intr(OptimizedPulseMeterSensor *sensor) {
  // This is an interrupt handler - we can't call any virtual method from this method.
  // Get the current time before we do anything else so the measurements are consistent.
  const uint32_t now = micros();
  const bool pin_val = sensor->isr_pin_.digital_read();
  auto &pulse_state = sensor->pulse_state_;
  auto &state = sensor->state_;

  // Filter length has passed since the last interrupt.
  const bool length = now - pulse_state.last_intr_ >= sensor->filter_us_;

  if (length && pulse_state.latched_ && !sensor->last_pin_val_) {
    // Long enough low edge.
    pulse_state.latched_ = false;
  } else if (length && !pulse_state.latched_ && sensor->last_pin_val_) {
    // Long enough high edge.
    pulse_state.latched_ = true;
    state.last_detected_edge_us_ = pulse_state.last_intr_;
    state.count_ += 1;
  }

  // Due to order of operations this includes:
  // length && latched && rising (just reset from a long low edge)
  // !latched && (rising || high) (noise on the line resetting the potential rising edge)
  state.last_rising_edge_us_ = !pulse_state.latched_ && pin_val ? now : state.last_detected_edge_us_;

  pulse_state.last_intr_ = now;
  sensor->last_pin_val_ = pin_val;
}

}  // namespace esphome::optimized_pulse_meter

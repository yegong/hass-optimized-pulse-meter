/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Derived from ESPHome pulse_meter component.
 */

#pragma once

#include "esphome/components/optimized_pulse_meter/optimized_pulse_meter_sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

namespace esphome::optimized_pulse_meter {

template<typename... Ts> class SetTotalAction final : public Action<Ts...> {
 public:
  SetTotalAction(OptimizedPulseMeterSensor *pulse_meter) : pulse_meter_(pulse_meter) {}

  TEMPLATABLE_VALUE(float, total)

  void play(const Ts &...x) override { this->pulse_meter_->set_total_value(this->total_.value(x...)); }

 protected:
  OptimizedPulseMeterSensor *pulse_meter_;
};

}  // namespace esphome::optimized_pulse_meter

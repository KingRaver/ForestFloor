#pragma once

#include <cstddef>

#include "ff/dsp/gain.hpp"

namespace ff::engine {

class Engine final {
 public:
  Engine() = default;

  void setMasterGain(float gain) noexcept;
  void process(float* mono_buffer, std::size_t frames) noexcept;

 private:
  ff::dsp::GainProcessor master_gain_;
};

}  // namespace ff::engine


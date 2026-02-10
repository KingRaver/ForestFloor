#pragma once

#include <cstddef>

namespace ff::dsp {

class GainProcessor final {
 public:
  void setGain(float gain) noexcept;
  void process(float* mono_buffer, std::size_t frames) const noexcept;

 private:
  float gain_ = 1.0F;
};

}  // namespace ff::dsp


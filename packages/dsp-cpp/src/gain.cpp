#include "ff/dsp/gain.hpp"

namespace ff::dsp {

void GainProcessor::setGain(float gain) noexcept { gain_ = gain; }

void GainProcessor::process(float* mono_buffer, std::size_t frames) const noexcept {
  if (mono_buffer == nullptr) {
    return;
  }

  for (std::size_t index = 0; index < frames; ++index) {
    mono_buffer[index] *= gain_;
  }
}

}  // namespace ff::dsp


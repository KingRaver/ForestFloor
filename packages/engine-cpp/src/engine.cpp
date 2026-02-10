#include "ff/engine/engine.hpp"

namespace ff::engine {

void Engine::setMasterGain(float gain) noexcept { master_gain_.setGain(gain); }

void Engine::process(float* mono_buffer, std::size_t frames) noexcept {
  master_gain_.process(mono_buffer, frames);
}

}  // namespace ff::engine


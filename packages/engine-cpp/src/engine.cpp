#include "ff/engine/engine.hpp"

#include <algorithm>
#include <utility>

namespace ff::engine {

void Engine::setMasterGain(float gain) noexcept { master_gain_.setGain(gain); }

void Engine::process(float* mono_buffer, std::size_t frames) noexcept {
  if (mono_buffer == nullptr || frames == 0) {
    return;
  }

  std::fill_n(mono_buffer, frames, 0.0F);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    float mixed_sample = 0.0F;
    for (auto& track : tracks_) {
      if (!track.active) {
        continue;
      }
      if (track.playhead >= track.sample.size()) {
        track.active = false;
        continue;
      }

      mixed_sample += track.sample[track.playhead] * track.velocity;
      ++track.playhead;
      if (track.playhead >= track.sample.size()) {
        track.active = false;
      }
    }
    mono_buffer[frame] = mixed_sample;
  }

  master_gain_.process(mono_buffer, frames);
}

bool Engine::setTrackSample(std::size_t track_index, std::vector<float> sample) {
  if (track_index >= kTrackCount || sample.empty()) {
    return false;
  }

  auto& track = tracks_[track_index];
  track.sample = std::move(sample);
  track.playhead = 0;
  track.active = false;
  track.velocity = 0.0F;
  return true;
}

void Engine::clearTrackSample(std::size_t track_index) noexcept {
  if (track_index >= kTrackCount) {
    return;
  }

  auto& track = tracks_[track_index];
  track.sample.clear();
  track.playhead = 0;
  track.velocity = 0.0F;
  track.active = false;
}

bool Engine::triggerTrack(std::size_t track_index, float velocity) noexcept {
  if (track_index >= kTrackCount) {
    return false;
  }

  auto& track = tracks_[track_index];
  if (track.sample.empty()) {
    return false;
  }

  track.playhead = 0;
  track.velocity = clampVelocity(velocity);
  track.active = track.velocity > 0.0F;
  return track.active;
}

bool Engine::handleMidiNoteOn(std::uint8_t note, std::uint8_t velocity) noexcept {
  if (velocity == 0) {
    return false;
  }

  if (note < pad_base_note_) {
    return false;
  }

  const std::size_t track_index = static_cast<std::size_t>(note - pad_base_note_);
  if (track_index >= kTrackCount) {
    return false;
  }

  return triggerTrack(track_index, static_cast<float>(velocity) / 127.0F);
}

void Engine::setPadBaseNote(std::uint8_t base_note) noexcept { pad_base_note_ = base_note; }

std::uint8_t Engine::padBaseNote() const noexcept { return pad_base_note_; }

void Engine::startTransport() noexcept { transport_.is_playing = true; }

void Engine::stopTransport() noexcept { transport_.is_playing = false; }

bool Engine::isTransportRunning() const noexcept { return transport_.is_playing; }

void Engine::setTempoBpm(float bpm) noexcept {
  transport_.bpm = std::clamp(bpm, 20.0F, 300.0F);
}

float Engine::tempoBpm() const noexcept { return transport_.bpm; }

bool Engine::setAudioDeviceConfig(AudioDeviceConfig config) {
  if (config.sample_rate_hz == 0 || config.buffer_size_frames == 0) {
    return false;
  }

  audio_device_config_ = std::move(config);
  return true;
}

AudioDeviceConfig Engine::audioDeviceConfig() const { return audio_device_config_; }

float Engine::clampVelocity(float velocity) noexcept {
  return std::clamp(velocity, 0.0F, 1.0F);
}

}  // namespace ff::engine

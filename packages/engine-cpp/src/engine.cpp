#include "ff/engine/engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <utility>

namespace ff::engine {

namespace {

float clampNormalized(float value) noexcept { return std::clamp(value, 0.0F, 1.0F); }

int normalizedToChokeGroup(float normalized) noexcept {
  const float clamped = clampNormalized(normalized);
  if (clamped <= 0.0001F) {
    return -1;
  }

  return std::clamp(static_cast<int>(std::lround(clamped * 16.0F)) - 1, 0, 15);
}

}  // namespace

void Engine::setMasterGain(float gain) noexcept { master_gain_.setGain(gain); }

void Engine::process(float* mono_buffer, std::size_t frames) noexcept {
  if (mono_buffer == nullptr || frames == 0) {
    return;
  }

  const auto started_at = profiling_enabled_ ? std::chrono::steady_clock::now()
                                             : std::chrono::steady_clock::time_point{};

  std::fill_n(mono_buffer, frames, 0.0F);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    float mixed_sample = 0.0F;
    for (auto& track : tracks_) {
      if (!track.active) {
        continue;
      }
      if (track.sample.empty()) {
        track.active = false;
        continue;
      }

      const float input = sampleAt(track);
      track.playhead += pitchRatio(track.parameters.pitch_semitones);
      if (track.playhead >= static_cast<double>(track.sample.size())) {
        track.active = false;
      }

      track.filter_state += filterAlpha(track.parameters.filter_cutoff) * (input - track.filter_state);
      const float amplitude = track.parameters.gain * track.trigger_velocity * track.envelope_value *
                              panGain(track.parameters.pan);
      mixed_sample += track.filter_state * amplitude;

      track.envelope_value *= envelopeCoefficient(track.parameters.envelope_decay);
      if (track.envelope_value < 0.0001F) {
        track.active = false;
      }
    }
    mono_buffer[frame] = mixed_sample;
  }

  master_gain_.process(mono_buffer, frames);

  if (profiling_enabled_) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started_at)
            .count();
    recordProcessTiming(frames, static_cast<double>(elapsed));
  }
}

bool Engine::setTrackSample(std::size_t track_index, std::vector<float> sample) {
  if (track_index >= kTrackCount || sample.empty()) {
    return false;
  }

  auto& track = tracks_[track_index];
  track.sample = std::move(sample);
  track.playhead = 0.0;
  track.active = false;
  track.trigger_velocity = 0.0F;
  track.envelope_value = 0.0F;
  track.filter_state = 0.0F;
  return true;
}

void Engine::clearTrackSample(std::size_t track_index) noexcept {
  if (track_index >= kTrackCount) {
    return;
  }

  auto& track = tracks_[track_index];
  track.sample.clear();
  track.playhead = 0.0;
  track.trigger_velocity = 0.0F;
  track.envelope_value = 0.0F;
  track.filter_state = 0.0F;
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

  if (track.parameters.choke_group >= 0) {
    for (std::size_t other_track_index = 0; other_track_index < kTrackCount; ++other_track_index) {
      if (other_track_index == track_index) {
        continue;
      }
      auto& other = tracks_[other_track_index];
      if (other.active && other.parameters.choke_group == track.parameters.choke_group) {
        other.active = false;
      }
    }
  }

  track.playhead = 0.0;
  track.trigger_velocity = clampVelocity(velocity);
  track.envelope_value = 1.0F;
  track.filter_state = 0.0F;
  track.active = track.trigger_velocity > 0.0F;
  return track.active;
}

bool Engine::setTrackParameters(std::size_t track_index, TrackParameters parameters) noexcept {
  if (track_index >= kTrackCount) {
    return false;
  }

  parameters.gain = clampGain(parameters.gain);
  parameters.pan = clampPan(parameters.pan);
  parameters.filter_cutoff = clampFilterCutoff(parameters.filter_cutoff);
  parameters.envelope_decay = clampEnvelopeDecay(parameters.envelope_decay);
  parameters.pitch_semitones = clampPitchSemitones(parameters.pitch_semitones);
  parameters.choke_group = clampChokeGroup(parameters.choke_group);
  tracks_[track_index].parameters = parameters;
  return true;
}

TrackParameters Engine::trackParameters(std::size_t track_index) const noexcept {
  if (track_index >= kTrackCount) {
    return TrackParameters{};
  }

  return tracks_[track_index].parameters;
}

bool Engine::applyParameterUpdate(std::uint32_t parameter_id, float normalized_value) noexcept {
  if (parameter_id < FF_PARAM_TRACK_BASE) {
    return false;
  }

  const std::uint32_t track_offset = parameter_id - FF_PARAM_TRACK_BASE;
  const std::size_t track_index = static_cast<std::size_t>(track_offset / FF_PARAM_TRACK_STRIDE);
  const std::uint32_t slot = track_offset % FF_PARAM_TRACK_STRIDE;
  if (track_index >= kTrackCount) {
    return false;
  }

  TrackParameters parameters = tracks_[track_index].parameters;
  const float clamped = clampNormalized(normalized_value);
  switch (slot) {
    case FF_PARAM_SLOT_GAIN:
      parameters.gain = clamped * 2.0F;
      break;
    case FF_PARAM_SLOT_PAN:
      parameters.pan = (clamped * 2.0F) - 1.0F;
      break;
    case FF_PARAM_SLOT_FILTER_CUTOFF:
      parameters.filter_cutoff = clamped;
      break;
    case FF_PARAM_SLOT_ENVELOPE_DECAY:
      parameters.envelope_decay = clamped;
      break;
    case FF_PARAM_SLOT_PITCH:
      parameters.pitch_semitones = (clamped * 48.0F) - 24.0F;
      break;
    case FF_PARAM_SLOT_CHOKE_GROUP:
      parameters.choke_group = normalizedToChokeGroup(clamped);
      break;
    default:
      return false;
  }

  return setTrackParameters(track_index, parameters);
}

bool Engine::applyParameterUpdates(const ff_parameter_update_t* updates, std::size_t count) noexcept {
  if (updates == nullptr) {
    return false;
  }

  bool all_applied = true;
  for (std::size_t index = 0; index < count; ++index) {
    const auto& update = updates[index];
    if (!applyParameterUpdate(update.parameter_id, update.normalized_value)) {
      all_applied = false;
    }
  }

  return all_applied;
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

void Engine::setProfilingEnabled(bool enabled) noexcept { profiling_enabled_ = enabled; }

bool Engine::profilingEnabled() const noexcept { return profiling_enabled_; }

void Engine::resetPerformanceStats() noexcept { performance_stats_ = PerformanceStats{}; }

PerformanceStats Engine::performanceStats() const noexcept { return performance_stats_; }

float Engine::clampVelocity(float velocity) noexcept {
  return std::clamp(velocity, 0.0F, 1.0F);
}

float Engine::clampGain(float gain) noexcept { return std::clamp(gain, 0.0F, 2.0F); }

float Engine::clampPan(float pan) noexcept { return std::clamp(pan, -1.0F, 1.0F); }

float Engine::clampFilterCutoff(float cutoff) noexcept {
  return std::clamp(cutoff, 0.0F, 1.0F);
}

float Engine::clampEnvelopeDecay(float decay) noexcept {
  return std::clamp(decay, 0.0F, 1.0F);
}

float Engine::clampPitchSemitones(float semitones) noexcept {
  return std::clamp(semitones, -24.0F, 24.0F);
}

int Engine::clampChokeGroup(int choke_group) noexcept {
  if (choke_group < 0) {
    return -1;
  }
  return std::min(choke_group, 15);
}

float Engine::sampleAt(const TrackVoice& track) const noexcept {
  if (track.sample.empty()) {
    return 0.0F;
  }

  const double clamped_playhead =
      std::clamp(track.playhead, 0.0, static_cast<double>(track.sample.size() - 1));
  const std::size_t lower_index = static_cast<std::size_t>(clamped_playhead);
  const std::size_t upper_index =
      std::min(lower_index + 1, static_cast<std::size_t>(track.sample.size() - 1));
  const float fraction = static_cast<float>(clamped_playhead - static_cast<double>(lower_index));
  const float lower_sample = track.sample[lower_index];
  const float upper_sample = track.sample[upper_index];
  return lower_sample + ((upper_sample - lower_sample) * fraction);
}

float Engine::pitchRatio(float semitones) const noexcept {
  return std::pow(2.0F, semitones / 12.0F);
}

float Engine::filterAlpha(float cutoff) const noexcept { return 0.01F + (cutoff * 0.99F); }

float Engine::envelopeCoefficient(float decay) const noexcept {
  const float sample_rate = static_cast<float>(std::max(audio_device_config_.sample_rate_hz, 1U));
  const float decay_seconds = 0.02F + (decay * 3.0F);
  return std::exp(-1.0F / (decay_seconds * sample_rate));
}

float Engine::panGain(float pan) const noexcept { return 1.0F - (std::fabs(pan) * 0.5F); }

void Engine::recordProcessTiming(std::size_t frames, double elapsed_us) noexcept {
  performance_stats_.processed_blocks += 1;
  performance_stats_.processed_frames += static_cast<std::uint64_t>(frames);
  performance_stats_.peak_block_duration_us =
      std::max(performance_stats_.peak_block_duration_us, elapsed_us);

  const double block_budget_us = blockBudgetMicros(frames);
  const double utilization = block_budget_us > 0.0 ? (elapsed_us / block_budget_us) : 0.0;
  performance_stats_.peak_callback_utilization =
      std::max(performance_stats_.peak_callback_utilization, utilization);
  if (utilization > 1.0) {
    performance_stats_.xrun_count += 1;
  }

  const double sample_count = static_cast<double>(performance_stats_.processed_blocks);
  if (sample_count <= 0.0) {
    return;
  }

  performance_stats_.average_block_duration_us +=
      (elapsed_us - performance_stats_.average_block_duration_us) / sample_count;
  performance_stats_.average_callback_utilization +=
      (utilization - performance_stats_.average_callback_utilization) / sample_count;
}

double Engine::blockBudgetMicros(std::size_t frames) const noexcept {
  const auto sample_rate_hz = audio_device_config_.sample_rate_hz;
  if (sample_rate_hz == 0) {
    return 0.0;
  }

  return (static_cast<double>(frames) * 1'000'000.0) /
         static_cast<double>(sample_rate_hz);
}

}  // namespace ff::engine

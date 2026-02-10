#include "runtime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ff/abi/contracts.h"

#include "sample_loader.hpp"

namespace ff::desktop {
namespace {

constexpr std::array<const char*, Runtime::kTrackCount> kStarterSampleNames = {
    "kick.wav",
    "snare.wav",
    "clap.wav",
    "hat_closed.wav",
    "hat_open.wav",
    "tom_low.wav",
    "tom_high.wav",
    "perc.wav",
};

std::vector<float> makeSyntheticFallbackSample(std::size_t track_index,
                                               std::uint32_t sample_rate_hz) {
  const std::size_t length = std::max<std::size_t>(512, sample_rate_hz / 8U);
  std::vector<float> sample(length, 0.0F);

  const float frequency = 45.0F + (12.0F * static_cast<float>(track_index));
  const float decay = 5.5F + (0.5F * static_cast<float>(track_index));
  constexpr float kTwoPi = 6.28318530717958647692F;

  for (std::size_t frame = 0; frame < sample.size(); ++frame) {
    const float time = static_cast<float>(frame) / static_cast<float>(sample_rate_hz);
    const float envelope = std::exp(-decay * time);
    const float sine = std::sin((kTwoPi * frequency * time) + (0.21F * static_cast<float>(track_index)));
    const float noise =
        std::sin(kTwoPi * (4'000.0F + (220.0F * static_cast<float>(track_index))) * time) * 0.2F;
    sample[frame] = std::clamp((sine * 0.85F + noise) * envelope, -1.0F, 1.0F);
  }

  return sample;
}

float clampVelocityToUnit(std::uint8_t velocity) noexcept {
  return std::clamp(static_cast<float>(velocity) / 127.0F, 0.0F, 1.0F);
}

std::string midiLearnBindingDescription(std::size_t track_index,
                                        std::uint8_t cc,
                                        MidiLearnSlot slot) {
  std::string slot_name;
  switch (slot) {
    case MidiLearnSlot::kTrackGain:
      slot_name = "gain";
      break;
    case MidiLearnSlot::kTrackFilterCutoff:
      slot_name = "filter_cutoff";
      break;
    case MidiLearnSlot::kTrackEnvelopeDecay:
      slot_name = "envelope_decay";
      break;
  }

  return "CC " + std::to_string(cc) + " -> track " +
         std::to_string(track_index + 1U) + " " + slot_name;
}

}  // namespace

Runtime::Runtime(ff::diagnostics::Reporter* diagnostics)
    : diagnostics_(diagnostics),
      audio_backend_(createAudioBackend()),
      midi_backend_(createMidiBackend()) {
  pending_commands_.reserve(256);

  for (auto& track_steps : steps_) {
    for (auto& step : track_steps) {
      step.store(0, std::memory_order_relaxed);
    }
  }
  for (auto& choke : track_choke_groups_) {
    choke.store(-1, std::memory_order_relaxed);
  }

  // Starter pattern defaults to a usable first-launch groove.
  setStep(0, 0, true, 127);
  setStep(0, 4, true, 120);
  setStep(0, 8, true, 127);
  setStep(0, 12, true, 120);

  setStep(1, 4, true, 118);
  setStep(1, 12, true, 118);

  for (std::size_t step = 0; step < kSteps; ++step) {
    if ((step % 2U) == 0U) {
      setStep(2, step, true, 95);
    }
  }

  setStep(3, 2, true, 90);
  setStep(3, 10, true, 90);

  ff::engine::TrackParameters hat_closed;
  hat_closed.choke_group = 1;
  hat_closed.envelope_decay = 0.25F;
  hat_closed.filter_cutoff = 0.8F;
  setTrackParameters(2, hat_closed);

  ff::engine::TrackParameters hat_open;
  hat_open.choke_group = 1;
  hat_open.envelope_decay = 0.65F;
  hat_open.filter_cutoff = 0.85F;
  setTrackParameters(4, hat_open);
}

Runtime::~Runtime() { stop(); }

bool Runtime::start(const RuntimeConfig& config, std::string* error_message) {
  if (running_.load(std::memory_order_acquire)) {
    return true;
  }

  config_ = config;
  if (config_.audio.sample_rate_hz == 0) {
    config_.audio.sample_rate_hz = 48'000;
  }
  if (config_.audio.buffer_size_frames == 0) {
    config_.audio.buffer_size_frames = 256;
  }
  if (config_.audio.device_id.empty()) {
    config_.audio.device_id = "default";
  }

  if (!engine_.setAudioDeviceConfig(config_.audio)) {
    if (error_message != nullptr) {
      *error_message = "invalid audio configuration";
    }
    return false;
  }

  engine_.setMasterGain(0.95F);
  engine_.setProfilingEnabled(true);
  engine_.resetPerformanceStats();
  engine_.setPadBaseNote(ff::engine::Engine::kDefaultPadBaseNote);

  // Ensure starter content is available on first launch.
  if (!loadStarterKit(error_message)) {
    return false;
  }

  AudioBackendConfig audio_config;
  audio_config.device_id = config_.audio.device_id;
  audio_config.sample_rate_hz = config_.audio.sample_rate_hz;
  audio_config.buffer_size_frames = config_.audio.buffer_size_frames;

  if (!audio_backend_->start(
          audio_config,
          [this](float* interleaved_output, std::uint32_t frames) {
            handleAudioCallback(interleaved_output, frames);
          },
          error_message)) {
    return false;
  }

  std::string midi_error;
  const bool midi_started = midi_backend_->start(
      config_.midi_device_id,
      [this](const std::uint8_t* bytes, std::size_t size) {
        handleMidiMessage(bytes, size);
      },
      &midi_error);

  running_.store(true, std::memory_order_release);

  if (diagnostics_ != nullptr) {
    const auto backend = audio_backend_->stats();
    diagnostics_->writeRuntimeReport(
        "desktop_runtime_started",
        {
            {"audio_device", config_.audio.device_id},
            {"sample_rate_hz", std::to_string(config_.audio.sample_rate_hz)},
            {"buffer_size_frames", std::to_string(config_.audio.buffer_size_frames)},
            {"midi_started", midi_started ? "yes" : "no"},
            {"midi_error", midi_error},
            {"backend_callback_count", std::to_string(backend.callback_count)},
        });
  }

  return true;
}

void Runtime::stop() {
  if (!running_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }

  midi_backend_->stop();
  audio_backend_->stop();
  transport_running_.store(false, std::memory_order_release);

  if (diagnostics_ != nullptr) {
    const auto backend = audio_backend_->stats();
    const auto perf = engine_.performanceStats();
    diagnostics_->writeRuntimeReport(
        "desktop_runtime_stopped",
        {
            {"backend_callback_count", std::to_string(backend.callback_count)},
            {"backend_xrun_count", std::to_string(backend.xrun_count)},
            {"engine_blocks", std::to_string(perf.processed_blocks)},
            {"engine_frames", std::to_string(perf.processed_frames)},
            {"engine_xruns", std::to_string(perf.xrun_count)},
        });
  }
}

bool Runtime::isRunning() const noexcept {
  return running_.load(std::memory_order_acquire);
}

void Runtime::setTransportRunning(bool running) {
  Command command;
  command.type = running ? CommandType::kStartTransport : CommandType::kStopTransport;
  if (!enqueueCommand(std::move(command))) {
    transport_running_.store(running, std::memory_order_release);
  }
}

void Runtime::toggleTransport() {
  setTransportRunning(!transportRunning());
}

bool Runtime::transportRunning() const noexcept {
  return transport_running_.load(std::memory_order_acquire);
}

void Runtime::setTempoBpm(float bpm) {
  const float clamped = std::clamp(bpm, 20.0F, 300.0F);
  tempo_bpm_.store(clamped, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(project_mutex_);
    project_model_.bpm = clamped;
  }

  Command command;
  command.type = CommandType::kSetTempo;
  command.value_a = clamped;
  if (!enqueueCommand(std::move(command))) {
    engine_.setTempoBpm(clamped);
  }
}

float Runtime::tempoBpm() const noexcept {
  return tempo_bpm_.load(std::memory_order_acquire);
}

void Runtime::setSwing(float swing) {
  const float clamped = std::clamp(swing, 0.0F, 0.45F);
  swing_.store(clamped, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(project_mutex_);
    project_model_.swing = clamped;
  }

  Command command;
  command.type = CommandType::kSetSwing;
  command.value_a = clamped;
  enqueueCommand(std::move(command));
}

float Runtime::swing() const noexcept {
  return swing_.load(std::memory_order_acquire);
}

bool Runtime::setStep(std::size_t track_index,
                      std::size_t step_index,
                      bool active,
                      std::uint8_t velocity) noexcept {
  if (track_index >= kTrackCount || step_index >= kSteps) {
    return false;
  }

  const std::uint8_t stored = active ? static_cast<std::uint8_t>(std::clamp<int>(velocity, 1, 127)) : 0;
  steps_[track_index][step_index].store(stored, std::memory_order_release);

  std::lock_guard<std::mutex> lock(project_mutex_);
  project_model_.pattern[track_index][step_index].active = active;
  project_model_.pattern[track_index][step_index].velocity = stored == 0 ? 100 : stored;
  return true;
}

ProjectStep Runtime::step(std::size_t track_index, std::size_t step_index) const noexcept {
  ProjectStep value{};
  if (track_index >= kTrackCount || step_index >= kSteps) {
    return value;
  }

  const std::uint8_t stored = steps_[track_index][step_index].load(std::memory_order_acquire);
  value.active = stored > 0;
  value.velocity = stored > 0 ? stored : 100;
  return value;
}

bool Runtime::triggerPad(std::size_t track_index, std::uint8_t velocity) noexcept {
  if (track_index >= kTrackCount || velocity == 0) {
    return false;
  }

  Command command;
  command.type = CommandType::kTriggerTrack;
  command.track_index = track_index;
  command.value_a = clampVelocityToUnit(velocity);
  if (!enqueueCommand(std::move(command))) {
    return engine_.triggerTrack(track_index, clampVelocityToUnit(velocity));
  }
  return true;
}

bool Runtime::setTrackParameters(std::size_t track_index,
                                 ff::engine::TrackParameters parameters) {
  if (track_index >= kTrackCount) {
    return false;
  }

  track_choke_groups_[track_index].store(parameters.choke_group,
                                         std::memory_order_release);

  {
    std::lock_guard<std::mutex> lock(project_mutex_);
    project_model_.tracks[track_index].parameters = parameters;
  }

  Command command;
  command.type = CommandType::kSetTrackParameters;
  command.track_index = track_index;
  command.track_parameters = parameters;
  if (!enqueueCommand(std::move(command))) {
    return engine_.setTrackParameters(track_index, parameters);
  }
  return true;
}

ff::engine::TrackParameters Runtime::trackParameters(std::size_t track_index) const {
  if (track_index >= kTrackCount) {
    return ff::engine::TrackParameters{};
  }

  std::lock_guard<std::mutex> lock(project_mutex_);
  return project_model_.tracks[track_index].parameters;
}

bool Runtime::setTrackSampleFromLoaded(std::size_t track_index,
                                       const LoadedSample& sample,
                                       const std::filesystem::path& path,
                                       std::string* error_message) {
  if (track_index >= kTrackCount || sample.mono.empty()) {
    if (error_message != nullptr) {
      *error_message = "invalid track/sample assignment";
    }
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(project_mutex_);
    project_model_.tracks[track_index].sample_path = path.string();
  }

  Command command;
  command.type = CommandType::kSetTrackSample;
  command.track_index = track_index;
  command.sample_data = sample.mono;
  if (!enqueueCommand(std::move(command))) {
    if (!engine_.setTrackSample(track_index, sample.mono)) {
      if (error_message != nullptr) {
        *error_message = "engine rejected sample assignment";
      }
      return false;
    }
  }

  return true;
}

bool Runtime::setTrackSampleFromFile(std::size_t track_index,
                                     const std::filesystem::path& path,
                                     std::string* error_message) {
  LoadedSample sample;
  if (!loadMonoSample(path, config_.audio.sample_rate_hz, &sample, error_message)) {
    return false;
  }

  return setTrackSampleFromLoaded(track_index, sample, path, error_message);
}

bool Runtime::loadStarterKit(std::string* error_message) {
  const std::filesystem::path root =
      std::filesystem::path(FF_SOURCE_ROOT) / "assets" / "starter-kit";
  const std::filesystem::path default_project = root / "default.ffproject";

  ProjectModel shipped_project;
  std::string shipped_error;
  if (loadProjectFromFile(default_project, &shipped_project, &shipped_error)) {
    setTempoBpm(shipped_project.bpm);
    setSwing(shipped_project.swing);

    for (std::size_t track = 0; track < kTrackCount; ++track) {
      for (std::size_t step = 0; step < kSteps; ++step) {
        const auto& cell = shipped_project.pattern[track][step];
        setStep(track, step, cell.active, cell.velocity);
      }
      setTrackParameters(track, shipped_project.tracks[track].parameters);
    }

    for (std::size_t track = 0; track < kTrackCount; ++track) {
      const auto& sample_path = shipped_project.tracks[track].sample_path;
      if (sample_path.empty()) {
        continue;
      }
      std::string load_error;
      if (!setTrackSampleFromFile(track, std::filesystem::path(sample_path), &load_error)) {
        if (error_message != nullptr) {
          *error_message = "failed loading shipped starter sample: " + sample_path +
                           " (" + load_error + ")";
        }
        return false;
      }
    }

    {
      std::lock_guard<std::mutex> lock(project_mutex_);
      project_model_ = shipped_project;
    }
    return true;
  }

  bool at_least_one_loaded = false;
  for (std::size_t track = 0; track < kTrackCount; ++track) {
    const auto sample_path = root / kStarterSampleNames[track];
    LoadedSample loaded;
    std::string load_error;
    if (loadMonoSample(sample_path, config_.audio.sample_rate_hz, &loaded, &load_error)) {
      at_least_one_loaded = true;
      setTrackSampleFromLoaded(track, loaded, sample_path, nullptr);
      continue;
    }

    loaded.source_sample_rate_hz = config_.audio.sample_rate_hz;
    loaded.mono = makeSyntheticFallbackSample(track, config_.audio.sample_rate_hz);
    setTrackSampleFromLoaded(track, loaded, sample_path, nullptr);
  }

  if (!at_least_one_loaded) {
    if (error_message != nullptr) {
      *error_message =
          "starter kit WAV assets not found and synthetic fallback was used";
    }
  } else if (error_message != nullptr && !shipped_error.empty()) {
    *error_message = "starter project parse failed, using direct WAV fallback: " +
                     shipped_error;
  }

  setTempoBpm(120.0F);
  setSwing(0.12F);
  return true;
}

bool Runtime::saveProject(const std::filesystem::path& path,
                          std::string* error_message) const {
  ProjectModel snapshot;
  {
    std::lock_guard<std::mutex> lock(project_mutex_);
    snapshot = project_model_;
  }
  return saveProjectToFile(path, snapshot, error_message);
}

bool Runtime::loadProject(const std::filesystem::path& path,
                          std::string* error_message) {
  ProjectModel loaded;
  if (!loadProjectFromFile(path, &loaded, error_message)) {
    return false;
  }

  setTempoBpm(loaded.bpm);
  setSwing(loaded.swing);

  for (std::size_t track = 0; track < kTrackCount; ++track) {
    for (std::size_t step_index = 0; step_index < kSteps; ++step_index) {
      const auto& cell = loaded.pattern[track][step_index];
      setStep(track, step_index, cell.active, cell.velocity);
    }
  }

  for (std::size_t track = 0; track < kTrackCount; ++track) {
    setTrackParameters(track, loaded.tracks[track].parameters);

    if (!loaded.tracks[track].sample_path.empty()) {
      std::string load_error;
      const std::filesystem::path sample_path(loaded.tracks[track].sample_path);
      if (!setTrackSampleFromFile(track, sample_path, &load_error)) {
        if (error_message != nullptr) {
          *error_message = "failed loading track sample: " + sample_path.string() +
                           " (" + load_error + ")";
        }
        return false;
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(project_mutex_);
    project_model_ = loaded;
  }

  return true;
}

bool Runtime::beginMidiLearn(std::size_t track_index, MidiLearnSlot slot) noexcept {
  if (track_index >= kTrackCount) {
    return false;
  }

  std::lock_guard<std::mutex> lock(midi_mutex_);
  active_learn_[0] = LearnTarget{track_index, slot};
  last_learned_binding_.reset();
  return true;
}

void Runtime::cancelMidiLearn() noexcept {
  std::lock_guard<std::mutex> lock(midi_mutex_);
  active_learn_[0].reset();
}

RuntimeStatus Runtime::status() const {
  RuntimeStatus status;
  status.audio_running = audio_backend_->isRunning();
  status.midi_running = midi_backend_->isRunning();
  status.transport_running = transportRunning();
  status.playhead_step = playhead_step_.load(std::memory_order_acquire);
  status.timeline_sample = timeline_sample_.load(std::memory_order_acquire);
  status.audio_device_id = config_.audio.device_id;

  const auto backend_stats = audio_backend_->stats();
  status.backend_xruns = backend_stats.xrun_count;

  const auto engine_stats = engine_.performanceStats();
  status.engine_xruns = engine_stats.xrun_count;

  if (diagnostics_ != nullptr) {
    status.diagnostics_directory = diagnostics_->outputDirectory().string();
  }

  const auto midi_devices = midi_backend_->inputDevices();
  status.midi_device_summary = std::to_string(midi_devices.size()) + " input(s)";

  {
    std::lock_guard<std::mutex> lock(midi_mutex_);
    status.learned_cc_binding = last_learned_binding_;
  }

  return status;
}

std::vector<AudioDeviceInfo> Runtime::audioOutputDevices() const {
  return audio_backend_->outputDevices();
}

std::vector<MidiDeviceInfo> Runtime::midiInputDevices() const {
  return midi_backend_->inputDevices();
}

const ProjectModel& Runtime::projectModelForUi() const noexcept {
  return project_model_;
}

std::filesystem::path Runtime::diagnosticsDirectory() const {
  if (diagnostics_ == nullptr) {
    return {};
  }
  return diagnostics_->outputDirectory();
}

bool Runtime::runHeadlessSession(std::uint32_t sample_rate_hz,
                                 std::uint32_t block_size_frames,
                                 std::size_t blocks,
                                 std::string* error_message) {
  if (sample_rate_hz == 0 || block_size_frames == 0 || blocks == 0) {
    if (error_message != nullptr) {
      *error_message = "invalid headless session parameters";
    }
    return false;
  }

  ff::engine::AudioDeviceConfig config;
  config.sample_rate_hz = sample_rate_hz;
  config.buffer_size_frames = block_size_frames;
  config.device_id = "headless";
  config_.audio = config;
  if (!engine_.setAudioDeviceConfig(config)) {
    if (error_message != nullptr) {
      *error_message = "failed setting headless audio configuration";
    }
    return false;
  }

  std::string starter_error;
  if (!loadStarterKit(&starter_error)) {
    if (error_message != nullptr) {
      *error_message = "failed loading starter kit for headless session: " +
                       starter_error;
    }
    return false;
  }

  setTransportRunning(true);

  std::vector<float> stereo(block_size_frames * 2U, 0.0F);
  float observed_peak = 0.0F;

  for (std::size_t block = 0; block < blocks; ++block) {
    handleAudioCallback(stereo.data(), block_size_frames);
    for (float sample : stereo) {
      if (!std::isfinite(sample)) {
        if (error_message != nullptr) {
          *error_message = "non-finite sample observed in headless render";
        }
        setTransportRunning(false);
        return false;
      }
      observed_peak = std::max(observed_peak, std::fabs(sample));
    }
  }

  setTransportRunning(false);

  if (observed_peak < 0.001F) {
    if (error_message != nullptr) {
      *error_message = "headless render produced silence";
    }
    return false;
  }

  return true;
}

bool Runtime::enqueueCommand(Command command) {
  std::lock_guard<std::mutex> lock(command_mutex_);
  if (pending_commands_.size() >= 4'096U) {
    return false;
  }

  pending_commands_.push_back(std::move(command));
  return true;
}

void Runtime::applyPendingCommands(ff::engine::Engine& engine,
                                   SequencerState& sequencer_state,
                                   std::vector<TriggerEvent>* immediate_events) {
  if (immediate_events == nullptr) {
    return;
  }

  std::vector<Command> commands;
  if (command_mutex_.try_lock()) {
    commands.swap(pending_commands_);
    command_mutex_.unlock();
  } else {
    return;
  }

  for (auto& command : commands) {
    switch (command.type) {
      case CommandType::kStartTransport:
        transport_running_.store(true, std::memory_order_release);
        engine.startTransport();
        sequencer_state.emit_step_on_next_process = true;
        sequencer_state.current_step = 0;
        sequencer_state.samples_to_next_step = stepIntervalSamples(0);
        playhead_step_.store(0, std::memory_order_release);
        break;
      case CommandType::kStopTransport:
        transport_running_.store(false, std::memory_order_release);
        engine.stopTransport();
        sequencer_state.emit_step_on_next_process = false;
        break;
      case CommandType::kSetTempo:
        engine.setTempoBpm(command.value_a);
        sequencer_state.samples_to_next_step =
            std::min(sequencer_state.samples_to_next_step,
                     stepIntervalSamples(sequencer_state.current_step));
        break;
      case CommandType::kSetSwing:
        sequencer_state.samples_to_next_step =
            std::min(sequencer_state.samples_to_next_step,
                     stepIntervalSamples(sequencer_state.current_step));
        break;
      case CommandType::kTriggerTrack:
        immediate_events->push_back(
            TriggerEvent{.offset = 0,
                         .track_index = command.track_index,
                         .velocity = std::clamp(command.value_a, 0.0F, 1.0F)});
        break;
      case CommandType::kSetTrackParameters:
        engine.setTrackParameters(command.track_index, command.track_parameters);
        break;
      case CommandType::kSetTrackSample:
        engine.setTrackSample(command.track_index, std::move(command.sample_data));
        break;
      case CommandType::kApplyEngineParameter:
        engine.applyParameterUpdate(command.parameter_id, command.value_a);
        break;
    }
  }
}

void Runtime::collectStepEvents(std::size_t step_index,
                                std::size_t block_offset,
                                std::vector<TriggerEvent>* events) const {
  if (events == nullptr || step_index >= kSteps) {
    return;
  }

  for (std::size_t track = 0; track < kTrackCount; ++track) {
    const std::uint8_t velocity =
        steps_[track][step_index].load(std::memory_order_acquire);
    if (velocity > 0) {
      events->push_back(TriggerEvent{
          .offset = block_offset,
          .track_index = track,
          .velocity = clampVelocityToUnit(velocity),
      });
    }
  }
}

void Runtime::processSequencer(std::uint32_t frames,
                               SequencerState& sequencer_state,
                               std::vector<TriggerEvent>* events) {
  if (events == nullptr || frames == 0 || !transportRunning()) {
    sequencer_state.timeline_sample += frames;
    timeline_sample_.store(sequencer_state.timeline_sample, std::memory_order_release);
    return;
  }

  if (sequencer_state.emit_step_on_next_process) {
    collectStepEvents(sequencer_state.current_step, 0, events);
    sequencer_state.emit_step_on_next_process = false;
    sequencer_state.samples_to_next_step = stepIntervalSamples(sequencer_state.current_step);
  }

  double remaining = static_cast<double>(frames);
  double consumed = 0.0;

  while (remaining > 0.0) {
    if (sequencer_state.samples_to_next_step <= remaining + std::numeric_limits<double>::epsilon()) {
      const double step_advance = std::max(0.0, sequencer_state.samples_to_next_step);
      consumed += step_advance;
      remaining -= step_advance;

      sequencer_state.current_step = (sequencer_state.current_step + 1U) % kSteps;
      playhead_step_.store(static_cast<std::uint32_t>(sequencer_state.current_step),
                           std::memory_order_release);

      const std::size_t offset = std::min<std::size_t>(
          frames,
          static_cast<std::size_t>(std::llround(consumed)));
      collectStepEvents(sequencer_state.current_step, offset, events);
      sequencer_state.samples_to_next_step = stepIntervalSamples(sequencer_state.current_step);
    } else {
      sequencer_state.samples_to_next_step -= remaining;
      remaining = 0.0;
    }
  }

  sequencer_state.timeline_sample += frames;
  timeline_sample_.store(sequencer_state.timeline_sample, std::memory_order_release);
}

double Runtime::stepIntervalSamples(std::size_t step_index) const noexcept {
  const double bpm = static_cast<double>(std::clamp(tempoBpm(), 20.0F, 300.0F));
  const auto sample_rate = static_cast<double>(
      std::max<std::uint32_t>(1, config_.audio.sample_rate_hz));
  const double base = sample_rate * 60.0 / bpm / 4.0;

  const double swing_amount = static_cast<double>(std::clamp(this->swing(), 0.0F, 0.45F));
  if (swing_amount <= std::numeric_limits<double>::epsilon()) {
    return base;
  }

  if ((step_index % 2U) == 0U) {
    return base * (1.0 + swing_amount);
  }
  return base * (1.0 - swing_amount);
}

void Runtime::handleAudioCallback(float* interleaved_output, std::uint32_t frames) {
  if (interleaved_output == nullptr || frames == 0) {
    return;
  }

  if (render_scratch_.size() < frames) {
    render_scratch_.resize(frames, 0.0F);
  }

  std::vector<TriggerEvent> events;
  events.reserve(64);

  applyPendingCommands(engine_, sequencer_, &events);
  processSequencer(frames, sequencer_, &events);

  std::sort(events.begin(), events.end(),
            [](const TriggerEvent& left, const TriggerEvent& right) {
              if (left.offset != right.offset) {
                return left.offset < right.offset;
              }
              return left.track_index < right.track_index;
            });

  std::size_t cursor = 0;
  std::size_t event_index = 0;

  while (event_index < events.size()) {
    const std::size_t event_offset = std::min<std::size_t>(events[event_index].offset, frames);
    if (event_offset > cursor) {
      engine_.process(render_scratch_.data() + cursor, event_offset - cursor);
      cursor = event_offset;
    }

    while (event_index < events.size() &&
           std::min<std::size_t>(events[event_index].offset, frames) == event_offset) {
      const auto& event = events[event_index];
      engine_.triggerTrack(event.track_index, event.velocity);
      ++event_index;
    }
  }

  if (cursor < frames) {
    engine_.process(render_scratch_.data() + cursor, frames - cursor);
  }

  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float mono = render_scratch_[frame];
    interleaved_output[(frame * 2U) + 0U] = mono;
    interleaved_output[(frame * 2U) + 1U] = mono;
  }
}

std::optional<std::uint32_t> Runtime::parameterIdForLearnTarget(
    const LearnTarget& target) noexcept {
  if (target.track_index >= kTrackCount) {
    return std::nullopt;
  }

  std::uint32_t slot = FF_PARAM_SLOT_GAIN;
  switch (target.slot) {
    case MidiLearnSlot::kTrackGain:
      slot = FF_PARAM_SLOT_GAIN;
      break;
    case MidiLearnSlot::kTrackFilterCutoff:
      slot = FF_PARAM_SLOT_FILTER_CUTOFF;
      break;
    case MidiLearnSlot::kTrackEnvelopeDecay:
      slot = FF_PARAM_SLOT_ENVELOPE_DECAY;
      break;
  }

  return FF_PARAM_TRACK_BASE +
         static_cast<std::uint32_t>(target.track_index) * FF_PARAM_TRACK_STRIDE +
         slot;
}

void Runtime::handleMidiMessage(const std::uint8_t* bytes, std::size_t size) {
  if (bytes == nullptr || size < 3U) {
    return;
  }

  const std::uint8_t status = bytes[0] & 0xF0U;
  const std::uint8_t data1 = bytes[1] & 0x7FU;
  const std::uint8_t data2 = bytes[2] & 0x7FU;

  if (status == 0x90U && data2 > 0) {
    const std::uint8_t base_note = engine_.padBaseNote();
    if (data1 >= base_note) {
      const std::size_t track_index = static_cast<std::size_t>(data1 - base_note);
      if (track_index < kTrackCount) {
        triggerPad(track_index, data2);
      }
    }
    return;
  }

  if (status != 0xB0U) {
    return;
  }

  std::optional<std::uint32_t> learned_parameter_id;
  {
    std::lock_guard<std::mutex> lock(midi_mutex_);
    if (active_learn_[0].has_value()) {
      learned_parameter_id = parameterIdForLearnTarget(active_learn_[0].value());
      if (learned_parameter_id.has_value()) {
        cc_bindings_[data1] = learned_parameter_id;
        last_learned_binding_ = midiLearnBindingDescription(
            active_learn_[0]->track_index,
            data1,
            active_learn_[0]->slot);
      }
      active_learn_[0].reset();
    } else {
      learned_parameter_id = cc_bindings_[data1];
    }
  }

  if (!learned_parameter_id.has_value()) {
    return;
  }

  Command command;
  command.type = CommandType::kApplyEngineParameter;
  command.parameter_id = learned_parameter_id.value();
  command.value_a = static_cast<float>(data2) / 127.0F;
  enqueueCommand(std::move(command));
}

void Runtime::refreshModelFromEngine() noexcept {
  std::lock_guard<std::mutex> lock(project_mutex_);
  for (std::size_t track = 0; track < kTrackCount; ++track) {
    project_model_.tracks[track].parameters = engine_.trackParameters(track);
  }
}

}  // namespace ff::desktop

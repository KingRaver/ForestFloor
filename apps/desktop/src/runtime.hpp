#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "audio_backend.hpp"
#include "midi_backend.hpp"
#include "project_io.hpp"
#include "sample_loader.hpp"

#include "ff/diagnostics/reporter.hpp"
#include "ff/engine/engine.hpp"

namespace ff::desktop {

enum class MidiLearnSlot {
  kTrackGain,
  kTrackFilterCutoff,
  kTrackEnvelopeDecay,
};

struct RuntimeConfig final {
  ff::engine::AudioDeviceConfig audio{};
  std::string midi_device_id = "default";
};

struct RuntimeStatus final {
  bool audio_running = false;
  bool midi_running = false;
  bool transport_running = false;
  std::uint32_t playhead_step = 0;
  std::uint64_t timeline_sample = 0;
  std::uint64_t backend_xruns = 0;
  std::uint64_t engine_xruns = 0;
  std::string audio_device_id;
  std::string midi_device_summary;
  std::string diagnostics_directory;
  std::optional<std::string> learned_cc_binding;
};

class Runtime final {
 public:
  static constexpr std::size_t kTrackCount = ff::engine::Engine::kTrackCount;
  static constexpr std::size_t kSteps = 16;

  explicit Runtime(ff::diagnostics::Reporter* diagnostics);
  ~Runtime();

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  bool start(const RuntimeConfig& config = RuntimeConfig{},
             std::string* error_message = nullptr);
  void stop();

  [[nodiscard]] bool isRunning() const noexcept;

  void setTransportRunning(bool running);
  void toggleTransport();
  [[nodiscard]] bool transportRunning() const noexcept;

  void setTempoBpm(float bpm);
  [[nodiscard]] float tempoBpm() const noexcept;

  void setSwing(float swing);
  [[nodiscard]] float swing() const noexcept;

  bool setStep(std::size_t track_index,
               std::size_t step_index,
               bool active,
               std::uint8_t velocity = 100) noexcept;
  [[nodiscard]] ProjectStep step(std::size_t track_index,
                                 std::size_t step_index) const noexcept;

  bool triggerPad(std::size_t track_index, std::uint8_t velocity) noexcept;

  bool setTrackParameters(std::size_t track_index,
                          ff::engine::TrackParameters parameters);
  [[nodiscard]] ff::engine::TrackParameters trackParameters(
      std::size_t track_index) const;

  bool setTrackSampleFromFile(std::size_t track_index,
                              const std::filesystem::path& path,
                              std::string* error_message = nullptr);
  bool loadStarterKit(std::string* error_message = nullptr);

  bool saveProject(const std::filesystem::path& path,
                   std::string* error_message = nullptr) const;
  bool loadProject(const std::filesystem::path& path,
                   std::string* error_message = nullptr);

  bool beginMidiLearn(std::size_t track_index, MidiLearnSlot slot) noexcept;
  void cancelMidiLearn() noexcept;

  [[nodiscard]] RuntimeStatus status() const;
  [[nodiscard]] std::vector<AudioDeviceInfo> audioOutputDevices() const;
  [[nodiscard]] std::vector<MidiDeviceInfo> midiInputDevices() const;

  [[nodiscard]] const ProjectModel& projectModelForUi() const noexcept;
  [[nodiscard]] std::filesystem::path diagnosticsDirectory() const;

  // Headless path used by CI smoke/soak checks to verify runtime wiring.
  bool runHeadlessSession(std::uint32_t sample_rate_hz,
                          std::uint32_t block_size_frames,
                          std::size_t blocks,
                          std::string* error_message = nullptr);

 private:
  enum class CommandType {
    kStartTransport,
    kStopTransport,
    kSetTempo,
    kSetSwing,
    kTriggerTrack,
    kSetTrackParameters,
    kSetTrackSample,
    kApplyEngineParameter,
  };

  struct Command final {
    CommandType type = CommandType::kStartTransport;
    std::size_t track_index = 0;
    float value_a = 0.0F;
    float value_b = 0.0F;
    ff::engine::TrackParameters track_parameters{};
    std::vector<float> sample_data;
    std::uint32_t parameter_id = 0;
  };

  struct LearnTarget final {
    std::size_t track_index = 0;
    MidiLearnSlot slot = MidiLearnSlot::kTrackGain;
  };

  struct SequencerState final {
    std::size_t current_step = 0;
    double samples_to_next_step = 0.0;
    std::uint64_t timeline_sample = 0;
    bool emit_step_on_next_process = false;
    std::size_t callbacks_since_start = 0;
  };

  struct TriggerEvent final {
    std::size_t offset = 0;
    std::size_t track_index = 0;
    float velocity = 0.0F;
  };

  bool enqueueCommand(Command command);
  void applyPendingCommands(ff::engine::Engine& engine,
                            SequencerState& sequencer_state,
                            std::vector<TriggerEvent>* immediate_events);
  void processSequencer(std::uint32_t frames,
                        SequencerState& sequencer_state,
                        std::vector<TriggerEvent>* events);
  void collectStepEvents(std::size_t step_index,
                         std::size_t block_offset,
                         std::vector<TriggerEvent>* events) const;
  [[nodiscard]] double stepIntervalSamples(std::size_t step_index) const noexcept;
  void handleAudioCallback(float* interleaved_output, std::uint32_t frames);

  void handleMidiMessage(const std::uint8_t* bytes, std::size_t size);
  static std::optional<std::uint32_t> parameterIdForLearnTarget(
      const LearnTarget& target) noexcept;

  bool setTrackSampleFromLoaded(std::size_t track_index,
                                const LoadedSample& sample,
                                const std::filesystem::path& path,
                                std::string* error_message);
  void refreshModelFromEngine() noexcept;

  ff::diagnostics::Reporter* diagnostics_ = nullptr;

  std::unique_ptr<AudioBackend> audio_backend_;
  std::unique_ptr<MidiBackend> midi_backend_;

  mutable std::mutex command_mutex_;
  std::vector<Command> pending_commands_;

  ff::engine::Engine engine_;
  SequencerState sequencer_{};

  std::array<std::array<std::atomic<std::uint8_t>, kSteps>, kTrackCount> steps_{};
  std::array<std::atomic<int>, kTrackCount> track_choke_groups_{};

  std::atomic<bool> transport_running_{false};
  std::atomic<float> tempo_bpm_{120.0F};
  std::atomic<float> swing_{0.0F};
  std::atomic<std::uint32_t> playhead_step_{0};
  std::atomic<std::uint64_t> timeline_sample_{0};
  std::atomic<bool> running_{false};

  mutable std::mutex project_mutex_;
  ProjectModel project_model_{};

  mutable std::mutex midi_mutex_;
  std::array<std::optional<LearnTarget>, 1> active_learn_{};
  std::array<std::optional<std::uint32_t>, 128> cc_bindings_{};
  std::optional<std::string> last_learned_binding_;

  std::vector<float> render_scratch_;

  RuntimeConfig config_{};
};

}  // namespace ff::desktop

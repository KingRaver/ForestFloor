#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ff/abi/contracts.h"

namespace ff::plugin_host {

inline constexpr std::uint32_t kSdkVersionMajor = 1;
inline constexpr std::uint32_t kSdkVersionMinor = 0;

enum class PluginClass : std::uint8_t {
  kInstrument = 1,
  kEffect = 2,
  kMidiProcessor = 3,
  kUtility = 4,
};

struct PluginEntrypoints final {
  bool has_create = false;
  bool has_prepare = false;
  bool has_process = false;
  bool has_reset = false;
  bool has_destroy = false;
};

struct PluginRuntimeInfo final {
  bool rt_safe_process = false;
  bool allows_dynamic_allocation = false;
  bool requests_process_isolation = false;
  bool has_unbounded_cpu_cost = false;
};

struct PluginBinaryInfo final {
  std::uint32_t sdk_version_major = 0;
  std::uint32_t sdk_version_minor = 0;
  PluginClass plugin_class = PluginClass::kInstrument;
  PluginEntrypoints entrypoints{};
  PluginRuntimeInfo runtime{};
};

struct PluginDescriptor final {
  std::string id;
  std::string name;
};

enum class ValidationSeverity : std::uint8_t {
  kError = 1,
  kWarning = 2,
};

struct ValidationIssue final {
  ValidationSeverity severity = ValidationSeverity::kError;
  std::string code;
  std::string message;
};

struct ValidationReport final {
  bool accepted = false;
  bool requires_isolation = false;
  std::vector<ValidationIssue> issues;
};

enum class LoadStatus : std::uint8_t {
  kRejected = 0,
  kLoadedInProcess = 1,
  kQueuedForIsolation = 2,
  kLoadError = 3,
};

struct LoadResult final {
  LoadStatus status = LoadStatus::kLoadError;
  ValidationReport validation{};
  std::string plugin_id;
  std::string message;
};

struct PluginRuntimeCounters final {
  std::uint32_t prepare_calls = 0;
  std::uint32_t process_calls = 0;
  std::uint32_t reset_calls = 0;
  std::uint32_t deactivate_calls = 0;
};

struct Route final {
  std::string source_id;
  std::string destination_id;
  float gain = 1.0F;
};

struct AutomationPoint final {
  std::uint64_t timeline_sample = 0;
  float normalized_value = 0.0F;
};

struct AutomationDispatch final {
  std::string plugin_id;
  ff_parameter_update_t parameter_update{};
};

struct PluginDescriptorC final {
  const char* id = nullptr;
  const char* name = nullptr;
};

struct PluginEntrypointsC final {
  std::uint8_t has_create = 0;
  std::uint8_t has_prepare = 0;
  std::uint8_t has_process = 0;
  std::uint8_t has_reset = 0;
  std::uint8_t has_destroy = 0;
};

struct PluginRuntimeInfoC final {
  std::uint8_t rt_safe_process = 0;
  std::uint8_t allows_dynamic_allocation = 0;
  std::uint8_t requests_process_isolation = 0;
  std::uint8_t has_unbounded_cpu_cost = 0;
};

struct PluginBinaryInfoC final {
  std::uint32_t sdk_version_major = 0;
  std::uint32_t sdk_version_minor = 0;
  std::uint32_t plugin_class = 0;
  PluginEntrypointsC entrypoints{};
  PluginRuntimeInfoC runtime{};
};

using PluginGetDescriptorFn = bool (*)(PluginDescriptorC* out_descriptor);
using PluginGetBinaryInfoFn = bool (*)(PluginBinaryInfoC* out_binary_info);
using PluginCreateFn = void* (*)(void* host_context);
using PluginPrepareFn = bool (*)(void* instance, double sample_rate_hz, std::uint32_t max_block_size,
                                 std::uint32_t channel_config);
using PluginProcessFn = void (*)(void* instance, std::uint32_t frames);
using PluginResetFn = void (*)(void* instance);
using PluginDestroyFn = void (*)(void* instance);

struct PluginLifecycleFns final {
  PluginCreateFn create = nullptr;
  PluginPrepareFn prepare = nullptr;
  PluginProcessFn process = nullptr;
  PluginResetFn reset = nullptr;
  PluginDestroyFn destroy = nullptr;
};

inline constexpr const char* kSymbolPluginGetDescriptorV1 = "ff_plugin_get_descriptor_v1";
inline constexpr const char* kSymbolPluginGetBinaryInfoV1 = "ff_plugin_get_binary_info_v1";
inline constexpr const char* kSymbolCreate = "ff_create";
inline constexpr const char* kSymbolPrepare = "ff_prepare";
inline constexpr const char* kSymbolProcess = "ff_process";
inline constexpr const char* kSymbolReset = "ff_reset";
inline constexpr const char* kSymbolDestroy = "ff_destroy";
inline constexpr const char* kRouteHostInput = "host.input";
inline constexpr const char* kRouteMasterOutput = "host.master";

class Host final {
 public:
  Host() = default;
  ~Host();
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;
  Host(Host&&) = delete;
  Host& operator=(Host&&) = delete;

  [[nodiscard]] ValidationReport validateBinary(const PluginDescriptor& descriptor,
                                                const PluginBinaryInfo& binary_info) const;
  [[nodiscard]] LoadResult loadPluginBinary(const std::string& binary_path);
  bool registerInternalPlugin(PluginDescriptor descriptor, PluginBinaryInfo binary_info,
                              PluginLifecycleFns lifecycle_fns);
  bool registerPlugin(PluginDescriptor descriptor, PluginBinaryInfo binary_info);
  bool registerPlugin(PluginDescriptor descriptor);
  bool activatePlugin(const std::string& plugin_id, double sample_rate_hz, std::uint32_t max_block_size,
                      std::uint32_t channel_config) noexcept;
  bool processPlugin(const std::string& plugin_id, std::uint32_t frames) noexcept;
  bool resetPlugin(const std::string& plugin_id) noexcept;
  bool deactivatePlugin(const std::string& plugin_id) noexcept;

  bool setRoute(Route route) noexcept;
  bool removeRoute(const std::string& source_id, const std::string& destination_id) noexcept;
  bool addAutomationPoint(const std::string& plugin_id, std::uint32_t parameter_id,
                          std::uint64_t timeline_sample, float normalized_value);
  [[nodiscard]] std::vector<AutomationDispatch> automationUpdatesAt(
      std::uint64_t timeline_sample) const;

  bool startIsolationSession(const std::string& plugin_id) noexcept;
  [[nodiscard]] std::size_t pluginCount() const noexcept;
  [[nodiscard]] std::size_t isolatedPluginCount() const noexcept;
  [[nodiscard]] std::size_t pendingIsolationCount() const noexcept;
  [[nodiscard]] std::size_t runningIsolationCount() const noexcept;
  [[nodiscard]] std::size_t routeCount() const noexcept;
  [[nodiscard]] std::size_t automationLaneCount() const noexcept;
  [[nodiscard]] PluginRuntimeCounters pluginRuntimeCounters(const std::string& plugin_id) const noexcept;

 private:
  struct AutomationLane final {
    std::string plugin_id;
    std::uint32_t parameter_id = 0;
    std::vector<AutomationPoint> points;
  };

  struct RegisteredPlugin final {
    PluginDescriptor descriptor;
    PluginBinaryInfo binary_info;
    PluginLifecycleFns lifecycle_fns{};
    PluginRuntimeCounters runtime_counters{};
    void* instance = nullptr;
    bool active = false;
    bool requires_isolation = false;
    std::string binary_path;
    bool isolation_pending = false;
    bool isolation_running = false;
    void* library_handle = nullptr;
  };

  static float clampNormalized(float normalized_value) noexcept;
  [[nodiscard]] static bool isValidEndpoint(const std::vector<RegisteredPlugin>& plugins,
                                            const std::string& endpoint, bool is_source) noexcept;
  [[nodiscard]] static std::optional<float> interpolateAutomationValue(
      const std::vector<AutomationPoint>& points, std::uint64_t timeline_sample) noexcept;
  static void closeLibraryHandle(void* library_handle) noexcept;
  [[nodiscard]] RegisteredPlugin* findPlugin(const std::string& plugin_id) noexcept;
  [[nodiscard]] const RegisteredPlugin* findPlugin(const std::string& plugin_id) const noexcept;
  [[nodiscard]] AutomationLane* findAutomationLane(const std::string& plugin_id,
                                                   std::uint32_t parameter_id) noexcept;
  [[nodiscard]] const AutomationLane* findAutomationLane(const std::string& plugin_id,
                                                         std::uint32_t parameter_id) const noexcept;
  [[nodiscard]] bool hasPluginId(const std::string& plugin_id) const noexcept;

  std::vector<RegisteredPlugin> plugins_;
  std::vector<Route> routes_;
  std::vector<AutomationLane> automation_lanes_;
};

}  // namespace ff::plugin_host

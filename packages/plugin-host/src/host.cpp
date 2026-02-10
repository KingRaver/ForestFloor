#include "ff/plugin_host/host.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

class DynamicLibrary final {
 public:
  DynamicLibrary() = default;
  ~DynamicLibrary() { close(); }

  DynamicLibrary(const DynamicLibrary&) = delete;
  DynamicLibrary& operator=(const DynamicLibrary&) = delete;
  DynamicLibrary(DynamicLibrary&&) = delete;
  DynamicLibrary& operator=(DynamicLibrary&&) = delete;

  bool open(const std::string& path, std::string* error) noexcept {
    close();
#if defined(_WIN32)
    const HMODULE module = LoadLibraryA(path.c_str());
    if (module == nullptr) {
      if (error != nullptr) {
        *error = "LoadLibraryA failed with error " + std::to_string(GetLastError());
      }
      return false;
    }
    handle_ = reinterpret_cast<void*>(module);
#else
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle_ == nullptr) {
      if (error != nullptr) {
        const char* dl_error = dlerror();
        *error = (dl_error != nullptr) ? dl_error : "unknown dlopen error";
      }
      return false;
    }
#endif
    return true;
  }

  void* symbol(const char* symbol_name) const noexcept {
    if (handle_ == nullptr || symbol_name == nullptr) {
      return nullptr;
    }
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle_), symbol_name));
#else
    return dlsym(handle_, symbol_name);
#endif
  }

  void* release() noexcept {
    void* released = handle_;
    handle_ = nullptr;
    return released;
  }

  void close() noexcept {
    if (handle_ == nullptr) {
      return;
    }
#if defined(_WIN32)
    (void)FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
    (void)dlclose(handle_);
#endif
    handle_ = nullptr;
  }

 private:
  void* handle_ = nullptr;
};

bool toBool(std::uint8_t value) noexcept { return value != 0; }

bool lifecycleFnsComplete(const ff::plugin_host::PluginLifecycleFns& lifecycle_fns) noexcept {
  return lifecycle_fns.create != nullptr && lifecycle_fns.prepare != nullptr &&
         lifecycle_fns.process != nullptr && lifecycle_fns.reset != nullptr &&
         lifecycle_fns.destroy != nullptr;
}

ff::plugin_host::PluginClass decodePluginClass(std::uint32_t raw_value,
                                               bool* is_valid) noexcept {
  using ff::plugin_host::PluginClass;
  switch (raw_value) {
    case static_cast<std::uint32_t>(PluginClass::kInstrument):
      if (is_valid != nullptr) {
        *is_valid = true;
      }
      return PluginClass::kInstrument;
    case static_cast<std::uint32_t>(PluginClass::kEffect):
      if (is_valid != nullptr) {
        *is_valid = true;
      }
      return PluginClass::kEffect;
    case static_cast<std::uint32_t>(PluginClass::kMidiProcessor):
      if (is_valid != nullptr) {
        *is_valid = true;
      }
      return PluginClass::kMidiProcessor;
    case static_cast<std::uint32_t>(PluginClass::kUtility):
      if (is_valid != nullptr) {
        *is_valid = true;
      }
      return PluginClass::kUtility;
    default:
      if (is_valid != nullptr) {
        *is_valid = false;
      }
      return PluginClass::kInstrument;
  }
}

ff::plugin_host::PluginEntrypoints mergeEntrypointsFromMetadataAndSymbols(
    const ff::plugin_host::PluginEntrypointsC& metadata,
    const DynamicLibrary& library) noexcept {
  return ff::plugin_host::PluginEntrypoints{
      .has_create =
          toBool(metadata.has_create) && (library.symbol(ff::plugin_host::kSymbolCreate) != nullptr),
      .has_prepare =
          toBool(metadata.has_prepare) && (library.symbol(ff::plugin_host::kSymbolPrepare) != nullptr),
      .has_process =
          toBool(metadata.has_process) && (library.symbol(ff::plugin_host::kSymbolProcess) != nullptr),
      .has_reset =
          toBool(metadata.has_reset) && (library.symbol(ff::plugin_host::kSymbolReset) != nullptr),
      .has_destroy =
          toBool(metadata.has_destroy) && (library.symbol(ff::plugin_host::kSymbolDestroy) != nullptr),
  };
}

ff::plugin_host::PluginRuntimeInfo decodeRuntimeInfo(
    const ff::plugin_host::PluginRuntimeInfoC& metadata) noexcept {
  return ff::plugin_host::PluginRuntimeInfo{
      .rt_safe_process = toBool(metadata.rt_safe_process),
      .allows_dynamic_allocation = toBool(metadata.allows_dynamic_allocation),
      .requests_process_isolation = toBool(metadata.requests_process_isolation),
      .has_unbounded_cpu_cost = toBool(metadata.has_unbounded_cpu_cost),
  };
}

ff::plugin_host::PluginLifecycleFns resolveLifecycleFns(
    const DynamicLibrary& library) noexcept {
  return ff::plugin_host::PluginLifecycleFns{
      .create =
          reinterpret_cast<ff::plugin_host::PluginCreateFn>(library.symbol(ff::plugin_host::kSymbolCreate)),
      .prepare = reinterpret_cast<ff::plugin_host::PluginPrepareFn>(
          library.symbol(ff::plugin_host::kSymbolPrepare)),
      .process = reinterpret_cast<ff::plugin_host::PluginProcessFn>(
          library.symbol(ff::plugin_host::kSymbolProcess)),
      .reset =
          reinterpret_cast<ff::plugin_host::PluginResetFn>(library.symbol(ff::plugin_host::kSymbolReset)),
      .destroy = reinterpret_cast<ff::plugin_host::PluginDestroyFn>(
          library.symbol(ff::plugin_host::kSymbolDestroy)),
  };
}

}  // namespace

namespace ff::plugin_host {

Host::~Host() {
  for (auto& plugin : plugins_) {
    if (plugin.active && plugin.lifecycle_fns.destroy != nullptr) {
      plugin.lifecycle_fns.destroy(plugin.instance);
      plugin.runtime_counters.deactivate_calls += 1;
      plugin.active = false;
      plugin.instance = nullptr;
    }
    closeLibraryHandle(plugin.library_handle);
    plugin.library_handle = nullptr;
  }
}

ValidationReport Host::validateBinary(const PluginDescriptor& descriptor,
                                      const PluginBinaryInfo& binary_info) const {
  ValidationReport report{};
  auto add_issue = [&](ValidationSeverity severity, const std::string& code,
                       const std::string& message) {
    report.issues.push_back(ValidationIssue{
        .severity = severity,
        .code = code,
        .message = message,
    });
  };

  if (descriptor.id.empty()) {
    add_issue(ValidationSeverity::kError, "descriptor.id.empty", "plugin id must not be empty");
  }

  if (descriptor.name.empty()) {
    add_issue(ValidationSeverity::kError, "descriptor.name.empty",
              "plugin name must not be empty");
  }

  if (binary_info.sdk_version_major != kSdkVersionMajor) {
    add_issue(ValidationSeverity::kError, "sdk.major.incompatible",
              "plugin SDK major version is incompatible with host");
  }

  if (!binary_info.entrypoints.has_create || !binary_info.entrypoints.has_prepare ||
      !binary_info.entrypoints.has_process || !binary_info.entrypoints.has_reset ||
      !binary_info.entrypoints.has_destroy) {
    add_issue(ValidationSeverity::kError, "entrypoints.missing",
              "plugin is missing one or more required lifecycle entrypoints");
  }

  if (!binary_info.runtime.rt_safe_process) {
    add_issue(ValidationSeverity::kError, "rt.process.unsafe",
              "plugin process callback is not marked real-time safe");
  }

  if (binary_info.runtime.allows_dynamic_allocation) {
    add_issue(ValidationSeverity::kError, "rt.dynamic_allocation",
              "plugin declares dynamic allocation on process callback");
  }

  if (binary_info.runtime.requests_process_isolation) {
    add_issue(ValidationSeverity::kWarning, "sandbox.isolation.requested",
              "plugin requested process-level isolation");
    report.requires_isolation = true;
  }

  if (binary_info.runtime.has_unbounded_cpu_cost) {
    add_issue(ValidationSeverity::kWarning, "sandbox.unbounded_cpu",
              "plugin reports unbounded CPU cost and should be isolated");
    report.requires_isolation = true;
  }

  report.accepted = true;
  for (const auto& issue : report.issues) {
    if (issue.severity == ValidationSeverity::kError) {
      report.accepted = false;
      break;
    }
  }
  return report;
}

LoadResult Host::loadPluginBinary(const std::string& binary_path) {
  LoadResult result{};
  if (binary_path.empty()) {
    result.status = LoadStatus::kLoadError;
    result.message = "binary path must not be empty";
    return result;
  }

  DynamicLibrary library;
  std::string load_error;
  if (!library.open(binary_path, &load_error)) {
    result.status = LoadStatus::kLoadError;
    result.message = "failed to open plugin binary: " + load_error;
    return result;
  }

  const auto get_descriptor =
      reinterpret_cast<PluginGetDescriptorFn>(library.symbol(kSymbolPluginGetDescriptorV1));
  const auto get_binary_info =
      reinterpret_cast<PluginGetBinaryInfoFn>(library.symbol(kSymbolPluginGetBinaryInfoV1));
  if (get_descriptor == nullptr || get_binary_info == nullptr) {
    result.status = LoadStatus::kLoadError;
    result.message = "plugin missing required metadata symbols";
    return result;
  }

  PluginDescriptorC descriptor_c{};
  PluginBinaryInfoC binary_info_c{};
  if (!get_descriptor(&descriptor_c) || !get_binary_info(&binary_info_c)) {
    result.status = LoadStatus::kLoadError;
    result.message = "plugin metadata export call failed";
    return result;
  }

  PluginDescriptor descriptor{};
  if (descriptor_c.id != nullptr) {
    descriptor.id = descriptor_c.id;
  }
  if (descriptor_c.name != nullptr) {
    descriptor.name = descriptor_c.name;
  }

  bool plugin_class_valid = false;
  const PluginLifecycleFns lifecycle_fns = resolveLifecycleFns(library);
  PluginBinaryInfo binary_info{
      .sdk_version_major = binary_info_c.sdk_version_major,
      .sdk_version_minor = binary_info_c.sdk_version_minor,
      .plugin_class = decodePluginClass(binary_info_c.plugin_class, &plugin_class_valid),
      .entrypoints = mergeEntrypointsFromMetadataAndSymbols(binary_info_c.entrypoints, library),
      .runtime = decodeRuntimeInfo(binary_info_c.runtime),
  };

  auto report = validateBinary(descriptor, binary_info);
  if (!plugin_class_valid) {
    report.issues.push_back(ValidationIssue{
        .severity = ValidationSeverity::kError,
        .code = "plugin.class.invalid",
        .message = "plugin class value is invalid",
    });
    report.accepted = false;
  }

  result.plugin_id = descriptor.id;
  result.validation = report;
  if (hasPluginId(descriptor.id)) {
    result.validation.issues.push_back(ValidationIssue{
        .severity = ValidationSeverity::kError,
        .code = "descriptor.id.duplicate",
        .message = "plugin id is already registered",
    });
    result.validation.accepted = false;
  }

  if (!result.validation.accepted) {
    result.status = LoadStatus::kRejected;
    result.message = "plugin rejected by validation";
    return result;
  }

  if (!result.validation.requires_isolation && !lifecycleFnsComplete(lifecycle_fns)) {
    result.status = LoadStatus::kLoadError;
    result.message = "plugin lifecycle symbols are incomplete";
    return result;
  }

  RegisteredPlugin plugin{
      .descriptor = std::move(descriptor),
      .binary_info = std::move(binary_info),
      .lifecycle_fns = lifecycle_fns,
      .runtime_counters = {},
      .instance = nullptr,
      .active = false,
      .requires_isolation = result.validation.requires_isolation,
      .binary_path = binary_path,
      .isolation_pending = result.validation.requires_isolation,
      .isolation_running = false,
      .library_handle = nullptr,
  };
  if (!result.validation.requires_isolation) {
    plugin.library_handle = library.release();
    result.status = LoadStatus::kLoadedInProcess;
    result.message = "plugin loaded in-process";
  } else {
    result.status = LoadStatus::kQueuedForIsolation;
    result.message = "plugin queued for isolated execution";
  }

  plugins_.push_back(std::move(plugin));
  return result;
}

bool Host::registerInternalPlugin(PluginDescriptor descriptor, PluginBinaryInfo binary_info,
                                  PluginLifecycleFns lifecycle_fns) {
  if (hasPluginId(descriptor.id)) {
    return false;
  }

  const auto report = validateBinary(descriptor, binary_info);
  if (!report.accepted || !lifecycleFnsComplete(lifecycle_fns)) {
    return false;
  }

  plugins_.push_back(RegisteredPlugin{
      .descriptor = std::move(descriptor),
      .binary_info = std::move(binary_info),
      .lifecycle_fns = lifecycle_fns,
      .runtime_counters = {},
      .instance = nullptr,
      .active = false,
      .requires_isolation = report.requires_isolation,
      .binary_path = "<internal>",
      .isolation_pending = report.requires_isolation,
      .isolation_running = false,
      .library_handle = nullptr,
  });
  return true;
}

bool Host::registerPlugin(PluginDescriptor descriptor, PluginBinaryInfo binary_info) {
  if (hasPluginId(descriptor.id)) {
    return false;
  }

  const auto report = validateBinary(descriptor, binary_info);
  if (!report.accepted) {
    return false;
  }

  plugins_.push_back(RegisteredPlugin{
      .descriptor = std::move(descriptor),
      .binary_info = std::move(binary_info),
      .lifecycle_fns = {},
      .runtime_counters = {},
      .instance = nullptr,
      .active = false,
      .requires_isolation = report.requires_isolation,
      .binary_path = "",
      .isolation_pending = report.requires_isolation,
      .isolation_running = false,
      .library_handle = nullptr,
  });
  return true;
}

bool Host::registerPlugin(PluginDescriptor descriptor) {
  PluginBinaryInfo default_binary{};
  default_binary.sdk_version_major = kSdkVersionMajor;
  default_binary.sdk_version_minor = kSdkVersionMinor;
  default_binary.entrypoints = PluginEntrypoints{
      .has_create = true,
      .has_prepare = true,
      .has_process = true,
      .has_reset = true,
      .has_destroy = true,
  };
  default_binary.runtime = PluginRuntimeInfo{
      .rt_safe_process = true,
      .allows_dynamic_allocation = false,
      .requests_process_isolation = false,
      .has_unbounded_cpu_cost = false,
  };
  return registerPlugin(std::move(descriptor), default_binary);
}

bool Host::activatePlugin(const std::string& plugin_id, double sample_rate_hz,
                          std::uint32_t max_block_size,
                          std::uint32_t channel_config) noexcept {
  auto* plugin = findPlugin(plugin_id);
  if (plugin == nullptr || plugin->active || plugin->requires_isolation ||
      plugin->lifecycle_fns.create == nullptr || plugin->lifecycle_fns.prepare == nullptr) {
    return false;
  }

  plugin->instance = plugin->lifecycle_fns.create(nullptr);
  if (!plugin->lifecycle_fns.prepare(plugin->instance, sample_rate_hz, max_block_size,
                                     channel_config)) {
    if (plugin->lifecycle_fns.destroy != nullptr) {
      plugin->lifecycle_fns.destroy(plugin->instance);
    }
    plugin->instance = nullptr;
    return false;
  }

  plugin->active = true;
  plugin->runtime_counters.prepare_calls += 1;
  return true;
}

bool Host::processPlugin(const std::string& plugin_id, std::uint32_t frames) noexcept {
  auto* plugin = findPlugin(plugin_id);
  if (plugin == nullptr || !plugin->active || plugin->lifecycle_fns.process == nullptr) {
    return false;
  }

  plugin->lifecycle_fns.process(plugin->instance, frames);
  plugin->runtime_counters.process_calls += 1;
  return true;
}

bool Host::resetPlugin(const std::string& plugin_id) noexcept {
  auto* plugin = findPlugin(plugin_id);
  if (plugin == nullptr || !plugin->active || plugin->lifecycle_fns.reset == nullptr) {
    return false;
  }

  plugin->lifecycle_fns.reset(plugin->instance);
  plugin->runtime_counters.reset_calls += 1;
  return true;
}

bool Host::deactivatePlugin(const std::string& plugin_id) noexcept {
  auto* plugin = findPlugin(plugin_id);
  if (plugin == nullptr || !plugin->active || plugin->lifecycle_fns.destroy == nullptr) {
    return false;
  }

  plugin->lifecycle_fns.destroy(plugin->instance);
  plugin->instance = nullptr;
  plugin->active = false;
  plugin->runtime_counters.deactivate_calls += 1;
  return true;
}

bool Host::setRoute(Route route) noexcept {
  if (route.source_id.empty() || route.destination_id.empty() ||
      route.source_id == route.destination_id ||
      !isValidEndpoint(plugins_, route.source_id, true) ||
      !isValidEndpoint(plugins_, route.destination_id, false)) {
    return false;
  }

  route.gain = std::clamp(route.gain, 0.0F, 2.0F);
  auto iterator =
      std::find_if(routes_.begin(), routes_.end(), [&](const Route& existing) {
        return existing.source_id == route.source_id &&
               existing.destination_id == route.destination_id;
      });
  if (iterator != routes_.end()) {
    iterator->gain = route.gain;
    return true;
  }

  routes_.push_back(std::move(route));
  return true;
}

bool Host::removeRoute(const std::string& source_id,
                       const std::string& destination_id) noexcept {
  auto iterator = std::find_if(routes_.begin(), routes_.end(), [&](const Route& existing) {
    return existing.source_id == source_id &&
           existing.destination_id == destination_id;
  });
  if (iterator == routes_.end()) {
    return false;
  }

  routes_.erase(iterator);
  return true;
}

bool Host::addAutomationPoint(const std::string& plugin_id, std::uint32_t parameter_id,
                              std::uint64_t timeline_sample, float normalized_value) {
  if (findPlugin(plugin_id) == nullptr) {
    return false;
  }

  auto* lane = findAutomationLane(plugin_id, parameter_id);
  if (lane == nullptr) {
    automation_lanes_.push_back(AutomationLane{
        .plugin_id = plugin_id,
        .parameter_id = parameter_id,
        .points = {},
    });
    lane = &automation_lanes_.back();
  }

  const float clamped = clampNormalized(normalized_value);
  auto iterator =
      std::find_if(lane->points.begin(), lane->points.end(),
                   [&](const AutomationPoint& point) {
                     return point.timeline_sample == timeline_sample;
                   });
  if (iterator != lane->points.end()) {
    iterator->normalized_value = clamped;
  } else {
    lane->points.push_back(AutomationPoint{
        .timeline_sample = timeline_sample,
        .normalized_value = clamped,
    });
    std::sort(lane->points.begin(), lane->points.end(),
              [](const AutomationPoint& left, const AutomationPoint& right) {
                return left.timeline_sample < right.timeline_sample;
              });
  }
  return true;
}

std::vector<AutomationDispatch> Host::automationUpdatesAt(
    std::uint64_t timeline_sample) const {
  std::vector<AutomationDispatch> updates;
  for (const auto& lane : automation_lanes_) {
    const auto value = interpolateAutomationValue(lane.points, timeline_sample);
    if (!value.has_value()) {
      continue;
    }

    updates.push_back(AutomationDispatch{
        .plugin_id = lane.plugin_id,
        .parameter_update =
            ff_parameter_update_t{
                .parameter_id = lane.parameter_id,
                .normalized_value = value.value(),
                .ramp_samples = 0,
                .reserved = 0,
            },
    });
  }
  return updates;
}

bool Host::startIsolationSession(const std::string& plugin_id) noexcept {
  auto* plugin = findPlugin(plugin_id);
  if (plugin == nullptr || !plugin->requires_isolation ||
      !plugin->isolation_pending) {
    return false;
  }

  plugin->isolation_pending = false;
  plugin->isolation_running = true;
  return true;
}

std::size_t Host::pluginCount() const noexcept { return plugins_.size(); }

std::size_t Host::isolatedPluginCount() const noexcept {
  return std::count_if(
      plugins_.begin(), plugins_.end(),
      [](const RegisteredPlugin& plugin) { return plugin.requires_isolation; });
}

std::size_t Host::pendingIsolationCount() const noexcept {
  return std::count_if(plugins_.begin(), plugins_.end(),
                       [](const RegisteredPlugin& plugin) {
                         return plugin.requires_isolation &&
                                plugin.isolation_pending;
                       });
}

std::size_t Host::runningIsolationCount() const noexcept {
  return std::count_if(plugins_.begin(), plugins_.end(),
                       [](const RegisteredPlugin& plugin) {
                         return plugin.requires_isolation &&
                                plugin.isolation_running;
                       });
}

std::size_t Host::routeCount() const noexcept { return routes_.size(); }

std::size_t Host::automationLaneCount() const noexcept {
  return automation_lanes_.size();
}

PluginRuntimeCounters Host::pluginRuntimeCounters(
    const std::string& plugin_id) const noexcept {
  const auto* plugin = findPlugin(plugin_id);
  if (plugin == nullptr) {
    return PluginRuntimeCounters{};
  }

  return plugin->runtime_counters;
}

float Host::clampNormalized(float normalized_value) noexcept {
  return std::clamp(normalized_value, 0.0F, 1.0F);
}

bool Host::isValidEndpoint(const std::vector<RegisteredPlugin>& plugins,
                           const std::string& endpoint,
                           bool is_source) noexcept {
  if (is_source && endpoint == kRouteHostInput) {
    return true;
  }
  if (!is_source && endpoint == kRouteMasterOutput) {
    return true;
  }

  return std::any_of(plugins.begin(), plugins.end(),
                     [&](const RegisteredPlugin& plugin) {
                       return plugin.descriptor.id == endpoint;
                     });
}

std::optional<float> Host::interpolateAutomationValue(
    const std::vector<AutomationPoint>& points,
    std::uint64_t timeline_sample) noexcept {
  if (points.empty()) {
    return std::nullopt;
  }
  if (timeline_sample <= points.front().timeline_sample) {
    return points.front().normalized_value;
  }
  if (timeline_sample >= points.back().timeline_sample) {
    return points.back().normalized_value;
  }

  for (std::size_t index = 1; index < points.size(); ++index) {
    const auto& previous = points[index - 1];
    const auto& current = points[index];
    if (timeline_sample <= current.timeline_sample) {
      const auto span = current.timeline_sample - previous.timeline_sample;
      if (span == 0) {
        return current.normalized_value;
      }

      const auto elapsed = timeline_sample - previous.timeline_sample;
      const float alpha = static_cast<float>(elapsed) /
                          static_cast<float>(span);
      return previous.normalized_value +
             ((current.normalized_value - previous.normalized_value) * alpha);
    }
  }

  return points.back().normalized_value;
}

void Host::closeLibraryHandle(void* library_handle) noexcept {
  if (library_handle == nullptr) {
    return;
  }

#if defined(_WIN32)
  (void)FreeLibrary(reinterpret_cast<HMODULE>(library_handle));
#else
  (void)dlclose(library_handle);
#endif
}

Host::RegisteredPlugin* Host::findPlugin(const std::string& plugin_id) noexcept {
  auto iterator = std::find_if(plugins_.begin(), plugins_.end(),
                               [&](const RegisteredPlugin& plugin) {
                                 return plugin.descriptor.id == plugin_id;
                               });
  if (iterator == plugins_.end()) {
    return nullptr;
  }

  return &(*iterator);
}

const Host::RegisteredPlugin* Host::findPlugin(
    const std::string& plugin_id) const noexcept {
  auto iterator = std::find_if(plugins_.begin(), plugins_.end(),
                               [&](const RegisteredPlugin& plugin) {
                                 return plugin.descriptor.id == plugin_id;
                               });
  if (iterator == plugins_.end()) {
    return nullptr;
  }

  return &(*iterator);
}

Host::AutomationLane* Host::findAutomationLane(const std::string& plugin_id,
                                               std::uint32_t parameter_id) noexcept {
  auto iterator =
      std::find_if(automation_lanes_.begin(), automation_lanes_.end(),
                   [&](const AutomationLane& lane) {
                     return lane.plugin_id == plugin_id &&
                            lane.parameter_id == parameter_id;
                   });
  if (iterator == automation_lanes_.end()) {
    return nullptr;
  }

  return &(*iterator);
}

const Host::AutomationLane* Host::findAutomationLane(
    const std::string& plugin_id, std::uint32_t parameter_id) const noexcept {
  auto iterator =
      std::find_if(automation_lanes_.begin(), automation_lanes_.end(),
                   [&](const AutomationLane& lane) {
                     return lane.plugin_id == plugin_id &&
                            lane.parameter_id == parameter_id;
                   });
  if (iterator == automation_lanes_.end()) {
    return nullptr;
  }

  return &(*iterator);
}

bool Host::hasPluginId(const std::string& plugin_id) const noexcept {
  return findPlugin(plugin_id) != nullptr;
}

}  // namespace ff::plugin_host

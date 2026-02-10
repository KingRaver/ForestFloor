#include "ff/plugin_host/host.hpp"

#include <cassert>
#include <cstdlib>
#include <string>

namespace {

struct InternalPluginState final {
  std::uint32_t processed_frames = 0;
};

InternalPluginState g_internal_state{};

std::string requiredEnv(const char* key) {
  const char* value = std::getenv(key);
  assert(value != nullptr);
  return value;
}

ff::plugin_host::PluginBinaryInfo validBinary() {
  ff::plugin_host::PluginBinaryInfo info;
  info.sdk_version_major = ff::plugin_host::kSdkVersionMajor;
  info.sdk_version_minor = ff::plugin_host::kSdkVersionMinor;
  info.plugin_class = ff::plugin_host::PluginClass::kInstrument;
  info.entrypoints = ff::plugin_host::PluginEntrypoints{
      .has_create = true,
      .has_prepare = true,
      .has_process = true,
      .has_reset = true,
      .has_destroy = true,
  };
  info.runtime = ff::plugin_host::PluginRuntimeInfo{
      .rt_safe_process = true,
      .allows_dynamic_allocation = false,
      .requests_process_isolation = false,
      .has_unbounded_cpu_cost = false,
  };
  return info;
}

void* internalCreate(void* /*host_context*/) {
  g_internal_state.processed_frames = 0;
  return &g_internal_state;
}

bool internalPrepare(void* instance, double sample_rate_hz,
                     std::uint32_t max_block_size,
                     std::uint32_t /*channel_config*/) {
  return instance != nullptr && sample_rate_hz > 0.0 &&
         max_block_size > 0;
}

void internalProcess(void* instance, std::uint32_t frames) {
  auto* state = reinterpret_cast<InternalPluginState*>(instance);
  if (state != nullptr) {
    state->processed_frames += frames;
  }
}

void internalReset(void* instance) {
  auto* state = reinterpret_cast<InternalPluginState*>(instance);
  if (state != nullptr) {
    state->processed_frames = 0;
  }
}

void internalDestroy(void* /*instance*/) {}

void rejectsIncompatibleSdkMajor() {
  ff::plugin_host::Host host;
  auto binary = validBinary();
  binary.sdk_version_major += 1;

  const auto report = host.validateBinary({"ff.external.fx", "External FX"}, binary);
  assert(!report.accepted);
  assert(!report.issues.empty());
}

void rejectsMissingEntrypoints() {
  ff::plugin_host::Host host;
  auto binary = validBinary();
  binary.entrypoints.has_process = false;
  assert(!host.registerPlugin({"ff.external.fx", "External FX"}, binary));
  assert(host.pluginCount() == 0);
}

void queuesIsolatedPluginAndStartsSession() {
  ff::plugin_host::Host host;
  const auto result = host.loadPluginBinary(requiredEnv("FF_TEST_PLUGIN_ISOLATED"));
  assert(result.status == ff::plugin_host::LoadStatus::kQueuedForIsolation);
  assert(result.validation.accepted);
  assert(result.validation.requires_isolation);
  assert(result.plugin_id == "ff.test.isolated");
  assert(host.pluginCount() == 1);
  assert(host.isolatedPluginCount() == 1);
  assert(host.pendingIsolationCount() == 1);
  assert(host.runningIsolationCount() == 0);
  assert(host.startIsolationSession("ff.test.isolated"));
  assert(host.pendingIsolationCount() == 0);
  assert(host.runningIsolationCount() == 1);
  assert(!host.startIsolationSession("ff.test.isolated"));
}

void rejectsDynamicPluginWithMissingProcessSymbol() {
  ff::plugin_host::Host host;
  const auto result = host.loadPluginBinary(requiredEnv("FF_TEST_PLUGIN_INVALID"));
  assert(result.status == ff::plugin_host::LoadStatus::kRejected);
  assert(!result.validation.accepted);
  assert(host.pluginCount() == 0);
}

void loadsInternalAndExternalPluginsViaSdkAndRunsLifecycle() {
  ff::plugin_host::Host host;

  auto binary = validBinary();
  const bool internal_registered = host.registerInternalPlugin(
      {"ff.internal.clock", "Internal Clock"}, binary,
      ff::plugin_host::PluginLifecycleFns{
          .create = internalCreate,
          .prepare = internalPrepare,
          .process = internalProcess,
          .reset = internalReset,
          .destroy = internalDestroy,
      });
  assert(internal_registered);

  const auto external_result = host.loadPluginBinary(requiredEnv("FF_TEST_PLUGIN_VALID"));
  assert(external_result.status == ff::plugin_host::LoadStatus::kLoadedInProcess);
  assert(external_result.validation.accepted);
  assert(external_result.plugin_id == "ff.test.valid");
  assert(host.pluginCount() == 2);

  assert(host.activatePlugin("ff.internal.clock", 48'000.0, 256, 0));
  assert(host.activatePlugin("ff.test.valid", 48'000.0, 256, 0));

  assert(host.processPlugin("ff.internal.clock", 128));
  assert(host.processPlugin("ff.internal.clock", 128));
  assert(host.processPlugin("ff.test.valid", 256));
  assert(host.resetPlugin("ff.internal.clock"));
  assert(host.resetPlugin("ff.test.valid"));
  assert(host.deactivatePlugin("ff.internal.clock"));
  assert(host.deactivatePlugin("ff.test.valid"));

  const auto internal_counters = host.pluginRuntimeCounters("ff.internal.clock");
  assert(internal_counters.prepare_calls == 1);
  assert(internal_counters.process_calls == 2);
  assert(internal_counters.reset_calls == 1);
  assert(internal_counters.deactivate_calls == 1);

  const auto external_counters = host.pluginRuntimeCounters("ff.test.valid");
  assert(external_counters.prepare_calls == 1);
  assert(external_counters.process_calls == 1);
  assert(external_counters.reset_calls == 1);
  assert(external_counters.deactivate_calls == 1);
}

void managesRoutesAndAutomationLanes() {
  ff::plugin_host::Host host;
  auto binary = validBinary();
  assert(host.registerInternalPlugin(
      {"ff.internal.clock", "Internal Clock"}, binary,
      ff::plugin_host::PluginLifecycleFns{
          .create = internalCreate,
          .prepare = internalPrepare,
          .process = internalProcess,
          .reset = internalReset,
          .destroy = internalDestroy,
      }));
  assert(host.loadPluginBinary(requiredEnv("FF_TEST_PLUGIN_VALID")).validation.accepted);

  assert(host.setRoute({.source_id = ff::plugin_host::kRouteHostInput,
                        .destination_id = "ff.internal.clock",
                        .gain = 1.0F}));
  assert(host.setRoute({.source_id = "ff.internal.clock",
                        .destination_id = "ff.test.valid",
                        .gain = 0.75F}));
  assert(host.setRoute({.source_id = "ff.test.valid",
                        .destination_id = ff::plugin_host::kRouteMasterOutput,
                        .gain = 0.9F}));
  assert(host.routeCount() == 3);
  assert(host.setRoute({.source_id = "ff.internal.clock",
                        .destination_id = "ff.test.valid",
                        .gain = 0.5F}));
  assert(host.routeCount() == 3);
  assert(host.removeRoute("ff.internal.clock", "ff.test.valid"));
  assert(host.routeCount() == 2);
  assert(!host.setRoute({.source_id = "ff.unknown",
                         .destination_id = "ff.test.valid",
                         .gain = 1.0F}));

  const std::uint32_t parameter_id = 0x5001;
  assert(host.addAutomationPoint("ff.test.valid", parameter_id, 0, 0.0F));
  assert(host.addAutomationPoint("ff.test.valid", parameter_id, 48'000, 1.0F));
  assert(host.addAutomationPoint("ff.test.valid", parameter_id, 24'000, 0.25F));
  assert(host.automationLaneCount() == 1);
  assert(!host.addAutomationPoint("ff.unknown", parameter_id, 0, 0.0F));

  const auto updates_start = host.automationUpdatesAt(0);
  assert(updates_start.size() == 1);
  assert(updates_start[0].plugin_id == "ff.test.valid");
  assert(updates_start[0].parameter_update.parameter_id == parameter_id);
  assert(updates_start[0].parameter_update.normalized_value == 0.0F);

  const auto updates_mid = host.automationUpdatesAt(12'000);
  assert(updates_mid.size() == 1);
  assert(updates_mid[0].parameter_update.normalized_value > 0.12F);
  assert(updates_mid[0].parameter_update.normalized_value < 0.13F);

  const auto updates_late = host.automationUpdatesAt(72'000);
  assert(updates_late.size() == 1);
  assert(updates_late[0].parameter_update.normalized_value == 1.0F);
}

void rejectsDuplicateDynamicPluginId() {
  ff::plugin_host::Host host;
  const auto valid_path = requiredEnv("FF_TEST_PLUGIN_VALID");
  const auto first = host.loadPluginBinary(valid_path);
  const auto second = host.loadPluginBinary(valid_path);
  assert(first.status == ff::plugin_host::LoadStatus::kLoadedInProcess);
  assert(second.status == ff::plugin_host::LoadStatus::kRejected);
  assert(host.pluginCount() == 1);
}

}  // namespace

int main() {
  rejectsIncompatibleSdkMajor();
  rejectsMissingEntrypoints();
  queuesIsolatedPluginAndStartsSession();
  rejectsDynamicPluginWithMissingProcessSymbol();
  loadsInternalAndExternalPluginsViaSdkAndRunsLifecycle();
  managesRoutesAndAutomationLanes();
  rejectsDuplicateDynamicPluginId();
  return 0;
}

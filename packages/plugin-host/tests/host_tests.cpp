#include "ff/plugin_host/host.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

#define FF_TEST_EXPECT(condition)                                                  \
  do {                                                                             \
    if (!(condition)) {                                                            \
      std::fprintf(stderr, "EXPECT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      std::fflush(stderr);                                                         \
      std::exit(1);                                                                \
    }                                                                              \
  } while (false)

struct InternalPluginState final {
  std::uint32_t processed_frames = 0;
};

InternalPluginState g_internal_state{};

struct NullCreateState final {
  std::uint32_t create_calls = 0;
  std::uint32_t prepare_calls = 0;
  std::uint32_t destroy_calls = 0;
};

NullCreateState g_null_create_state{};

struct PrepareFailureState final {
  std::uint32_t create_calls = 0;
  std::uint32_t prepare_calls = 0;
  std::uint32_t destroy_calls = 0;
  int instance_payload = 0;
};

PrepareFailureState g_prepare_failure_state{};

std::string requiredEnv(const char* key) {
  const char* value = std::getenv(key);
  FF_TEST_EXPECT(value != nullptr);
  return value;
}

void trustPluginBinaryRoot(ff::plugin_host::Host* host,
                           const std::string& binary_path) {
  FF_TEST_EXPECT(host != nullptr);
  const std::filesystem::path path(binary_path);
  const auto root = path.parent_path();
  FF_TEST_EXPECT(!root.empty());
  FF_TEST_EXPECT(host->addTrustedPluginRoot(root.string()));
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

void* nullCreate(void* /*host_context*/) {
  g_null_create_state.create_calls += 1;
  return nullptr;
}

bool nullCreatePrepare(void* /*instance*/, double /*sample_rate_hz*/,
                       std::uint32_t /*max_block_size*/,
                       std::uint32_t /*channel_config*/) {
  g_null_create_state.prepare_calls += 1;
  return true;
}

void nullCreateDestroy(void* /*instance*/) { g_null_create_state.destroy_calls += 1; }

void* prepareFailureCreate(void* /*host_context*/) {
  g_prepare_failure_state.create_calls += 1;
  return &g_prepare_failure_state.instance_payload;
}

bool prepareFailurePrepare(void* /*instance*/, double /*sample_rate_hz*/,
                           std::uint32_t /*max_block_size*/,
                           std::uint32_t /*channel_config*/) {
  g_prepare_failure_state.prepare_calls += 1;
  return false;
}

void prepareFailureDestroy(void* /*instance*/) { g_prepare_failure_state.destroy_calls += 1; }

void rejectsIncompatibleSdkMajor() {
  ff::plugin_host::Host host;
  auto binary = validBinary();
  binary.sdk_version_major += 1;

  const auto report = host.validateBinary({"ff.external.fx", "External FX"}, binary);
  FF_TEST_EXPECT(!report.accepted);
  FF_TEST_EXPECT(!report.issues.empty());
}

void rejectsMissingEntrypoints() {
  ff::plugin_host::Host host;
  auto binary = validBinary();
  binary.entrypoints.has_process = false;
  FF_TEST_EXPECT(!host.registerPlugin({"ff.external.fx", "External FX"}, binary));
  FF_TEST_EXPECT(host.pluginCount() == 0);
}

void queuesIsolatedPluginAndStartsSession() {
  ff::plugin_host::Host host;
  const auto path = requiredEnv("FF_TEST_PLUGIN_ISOLATED");
  trustPluginBinaryRoot(&host, path);
  const auto result = host.loadPluginBinary(path);
  FF_TEST_EXPECT(result.status == ff::plugin_host::LoadStatus::kQueuedForIsolation);
  FF_TEST_EXPECT(result.validation.accepted);
  FF_TEST_EXPECT(result.validation.requires_isolation);
  FF_TEST_EXPECT(result.plugin_id == "ff.test.isolated");
  FF_TEST_EXPECT(host.pluginCount() == 1);
  FF_TEST_EXPECT(host.isolatedPluginCount() == 1);
  FF_TEST_EXPECT(host.pendingIsolationCount() == 1);
  FF_TEST_EXPECT(host.runningIsolationCount() == 0);
  FF_TEST_EXPECT(host.startIsolationSession("ff.test.isolated"));
  FF_TEST_EXPECT(host.pendingIsolationCount() == 0);
  FF_TEST_EXPECT(host.runningIsolationCount() == 1);
  FF_TEST_EXPECT(!host.startIsolationSession("ff.test.isolated"));
}

void rejectsDynamicPluginWithMissingProcessSymbol() {
  ff::plugin_host::Host host;
  const auto path = requiredEnv("FF_TEST_PLUGIN_INVALID");
  trustPluginBinaryRoot(&host, path);
  const auto result = host.loadPluginBinary(path);
  FF_TEST_EXPECT(result.status == ff::plugin_host::LoadStatus::kRejected);
  FF_TEST_EXPECT(!result.validation.accepted);
  FF_TEST_EXPECT(host.pluginCount() == 0);
}

void rejectsActivationWhenCreateReturnsNull() {
  ff::plugin_host::Host host;
  g_null_create_state = {};

  auto binary = validBinary();
  FF_TEST_EXPECT(host.registerInternalPlugin(
      {"ff.internal.null-create", "Null Create"}, binary,
      ff::plugin_host::PluginLifecycleFns{
          .create = nullCreate,
          .prepare = nullCreatePrepare,
          .process = internalProcess,
          .reset = internalReset,
          .destroy = nullCreateDestroy,
      }));

  FF_TEST_EXPECT(!host.activatePlugin("ff.internal.null-create", 48'000.0, 256, 0));
  FF_TEST_EXPECT(g_null_create_state.create_calls == 1);
  FF_TEST_EXPECT(g_null_create_state.prepare_calls == 0);
  FF_TEST_EXPECT(g_null_create_state.destroy_calls == 0);

  const auto counters = host.pluginRuntimeCounters("ff.internal.null-create");
  FF_TEST_EXPECT(counters.prepare_calls == 0);
  FF_TEST_EXPECT(counters.process_calls == 0);
  FF_TEST_EXPECT(counters.reset_calls == 0);
  FF_TEST_EXPECT(counters.deactivate_calls == 0);

  FF_TEST_EXPECT(!host.processPlugin("ff.internal.null-create", 64));
  FF_TEST_EXPECT(!host.resetPlugin("ff.internal.null-create"));
  FF_TEST_EXPECT(!host.deactivatePlugin("ff.internal.null-create"));
}

void destroysInstanceWhenPrepareFails() {
  ff::plugin_host::Host host;
  g_prepare_failure_state = {};

  auto binary = validBinary();
  FF_TEST_EXPECT(host.registerInternalPlugin(
      {"ff.internal.prepare-failure", "Prepare Failure"}, binary,
      ff::plugin_host::PluginLifecycleFns{
          .create = prepareFailureCreate,
          .prepare = prepareFailurePrepare,
          .process = internalProcess,
          .reset = internalReset,
          .destroy = prepareFailureDestroy,
      }));

  FF_TEST_EXPECT(!host.activatePlugin("ff.internal.prepare-failure", 48'000.0, 256, 0));
  FF_TEST_EXPECT(g_prepare_failure_state.create_calls == 1);
  FF_TEST_EXPECT(g_prepare_failure_state.prepare_calls == 1);
  FF_TEST_EXPECT(g_prepare_failure_state.destroy_calls == 1);

  const auto counters = host.pluginRuntimeCounters("ff.internal.prepare-failure");
  FF_TEST_EXPECT(counters.prepare_calls == 0);
  FF_TEST_EXPECT(counters.process_calls == 0);
  FF_TEST_EXPECT(counters.reset_calls == 0);
  FF_TEST_EXPECT(counters.deactivate_calls == 0);

  FF_TEST_EXPECT(!host.processPlugin("ff.internal.prepare-failure", 64));
  FF_TEST_EXPECT(!host.resetPlugin("ff.internal.prepare-failure"));
  FF_TEST_EXPECT(!host.deactivatePlugin("ff.internal.prepare-failure"));
}

void rejectsDynamicPluginOutsideTrustedRoots() {
  ff::plugin_host::Host host;
  const auto path = requiredEnv("FF_TEST_PLUGIN_VALID");
  const auto result = host.loadPluginBinary(path);
  FF_TEST_EXPECT(result.status == ff::plugin_host::LoadStatus::kRejected);
  FF_TEST_EXPECT(!result.validation.accepted);
  FF_TEST_EXPECT(result.validation.issues.size() == 1);
  FF_TEST_EXPECT(result.validation.issues[0].code == "trust.path.untrusted");
  FF_TEST_EXPECT(host.pluginCount() == 0);
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
  FF_TEST_EXPECT(internal_registered);

  const auto external_path = requiredEnv("FF_TEST_PLUGIN_VALID");
  trustPluginBinaryRoot(&host, external_path);
  const auto external_result = host.loadPluginBinary(external_path);
  FF_TEST_EXPECT(external_result.status == ff::plugin_host::LoadStatus::kLoadedInProcess);
  FF_TEST_EXPECT(external_result.validation.accepted);
  FF_TEST_EXPECT(external_result.plugin_id == "ff.test.valid");
  FF_TEST_EXPECT(host.pluginCount() == 2);

  FF_TEST_EXPECT(host.activatePlugin("ff.internal.clock", 48'000.0, 256, 0));
  FF_TEST_EXPECT(host.activatePlugin("ff.test.valid", 48'000.0, 256, 0));

  FF_TEST_EXPECT(host.processPlugin("ff.internal.clock", 128));
  FF_TEST_EXPECT(host.processPlugin("ff.internal.clock", 128));
  FF_TEST_EXPECT(host.processPlugin("ff.test.valid", 256));
  FF_TEST_EXPECT(host.resetPlugin("ff.internal.clock"));
  FF_TEST_EXPECT(host.resetPlugin("ff.test.valid"));
  FF_TEST_EXPECT(host.deactivatePlugin("ff.internal.clock"));
  FF_TEST_EXPECT(host.deactivatePlugin("ff.test.valid"));

  const auto internal_counters = host.pluginRuntimeCounters("ff.internal.clock");
  FF_TEST_EXPECT(internal_counters.prepare_calls == 1);
  FF_TEST_EXPECT(internal_counters.process_calls == 2);
  FF_TEST_EXPECT(internal_counters.reset_calls == 1);
  FF_TEST_EXPECT(internal_counters.deactivate_calls == 1);

  const auto external_counters = host.pluginRuntimeCounters("ff.test.valid");
  FF_TEST_EXPECT(external_counters.prepare_calls == 1);
  FF_TEST_EXPECT(external_counters.process_calls == 1);
  FF_TEST_EXPECT(external_counters.reset_calls == 1);
  FF_TEST_EXPECT(external_counters.deactivate_calls == 1);
}

void managesRoutesAndAutomationLanes() {
  ff::plugin_host::Host host;
  auto binary = validBinary();
  FF_TEST_EXPECT(host.registerInternalPlugin(
      {"ff.internal.clock", "Internal Clock"}, binary,
      ff::plugin_host::PluginLifecycleFns{
          .create = internalCreate,
          .prepare = internalPrepare,
          .process = internalProcess,
          .reset = internalReset,
          .destroy = internalDestroy,
      }));
  const auto external_path = requiredEnv("FF_TEST_PLUGIN_VALID");
  trustPluginBinaryRoot(&host, external_path);
  FF_TEST_EXPECT(host.loadPluginBinary(external_path).validation.accepted);

  FF_TEST_EXPECT(host.setRoute({.source_id = ff::plugin_host::kRouteHostInput,
                        .destination_id = "ff.internal.clock",
                        .gain = 1.0F}));
  FF_TEST_EXPECT(host.setRoute({.source_id = "ff.internal.clock",
                        .destination_id = "ff.test.valid",
                        .gain = 0.75F}));
  FF_TEST_EXPECT(host.setRoute({.source_id = "ff.test.valid",
                        .destination_id = ff::plugin_host::kRouteMasterOutput,
                        .gain = 0.9F}));
  FF_TEST_EXPECT(host.routeCount() == 3);
  FF_TEST_EXPECT(host.setRoute({.source_id = "ff.internal.clock",
                        .destination_id = "ff.test.valid",
                        .gain = 0.5F}));
  FF_TEST_EXPECT(host.routeCount() == 3);
  FF_TEST_EXPECT(host.removeRoute("ff.internal.clock", "ff.test.valid"));
  FF_TEST_EXPECT(host.routeCount() == 2);
  FF_TEST_EXPECT(!host.setRoute({.source_id = "ff.unknown",
                         .destination_id = "ff.test.valid",
                         .gain = 1.0F}));

  const std::uint32_t parameter_id = 0x5001;
  FF_TEST_EXPECT(host.addAutomationPoint("ff.test.valid", parameter_id, 0, 0.0F));
  FF_TEST_EXPECT(host.addAutomationPoint("ff.test.valid", parameter_id, 48'000, 1.0F));
  FF_TEST_EXPECT(host.addAutomationPoint("ff.test.valid", parameter_id, 24'000, 0.25F));
  FF_TEST_EXPECT(host.automationLaneCount() == 1);
  FF_TEST_EXPECT(!host.addAutomationPoint("ff.unknown", parameter_id, 0, 0.0F));

  const auto updates_start = host.automationUpdatesAt(0);
  FF_TEST_EXPECT(updates_start.size() == 1);
  FF_TEST_EXPECT(updates_start[0].plugin_id == "ff.test.valid");
  FF_TEST_EXPECT(updates_start[0].parameter_update.parameter_id == parameter_id);
  FF_TEST_EXPECT(updates_start[0].parameter_update.normalized_value == 0.0F);

  const auto updates_mid = host.automationUpdatesAt(12'000);
  FF_TEST_EXPECT(updates_mid.size() == 1);
  FF_TEST_EXPECT(updates_mid[0].parameter_update.normalized_value > 0.12F);
  FF_TEST_EXPECT(updates_mid[0].parameter_update.normalized_value < 0.13F);

  const auto updates_late = host.automationUpdatesAt(72'000);
  FF_TEST_EXPECT(updates_late.size() == 1);
  FF_TEST_EXPECT(updates_late[0].parameter_update.normalized_value == 1.0F);
}

void rejectsDuplicateDynamicPluginId() {
  ff::plugin_host::Host host;
  const auto valid_path = requiredEnv("FF_TEST_PLUGIN_VALID");
  trustPluginBinaryRoot(&host, valid_path);
  const auto first = host.loadPluginBinary(valid_path);
  const auto second = host.loadPluginBinary(valid_path);
  FF_TEST_EXPECT(first.status == ff::plugin_host::LoadStatus::kLoadedInProcess);
  FF_TEST_EXPECT(second.status == ff::plugin_host::LoadStatus::kRejected);
  FF_TEST_EXPECT(host.pluginCount() == 1);
}

}  // namespace

int main() {
#if defined(_WIN32)
  // Suppress Windows error dialogs (missing DLL, crash reporting) that would
  // block indefinitely on headless CI runners.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
  rejectsIncompatibleSdkMajor();
  rejectsMissingEntrypoints();
  rejectsDynamicPluginOutsideTrustedRoots();
  queuesIsolatedPluginAndStartsSession();
  rejectsDynamicPluginWithMissingProcessSymbol();
  rejectsActivationWhenCreateReturnsNull();
  destroysInstanceWhenPrepareFails();
  loadsInternalAndExternalPluginsViaSdkAndRunsLifecycle();
  managesRoutesAndAutomationLanes();
  rejectsDuplicateDynamicPluginId();
  return 0;
}

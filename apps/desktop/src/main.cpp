#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "ff/engine/engine.hpp"
#include "ff/plugin_host/host.hpp"

namespace {

struct InternalSamplerState final {
  std::uint32_t processed_frames = 0;
};

void* internalCreate(void* /*host_context*/) {
  static InternalSamplerState state{};
  state.processed_frames = 0;
  return &state;
}

bool internalPrepare(void* instance, double sample_rate_hz,
                     std::uint32_t max_block_size,
                     std::uint32_t /*channel_config*/) {
  return instance != nullptr && sample_rate_hz > 0.0 &&
         max_block_size > 0;
}

void internalProcess(void* instance, std::uint32_t frames) {
  auto* state = reinterpret_cast<InternalSamplerState*>(instance);
  if (state != nullptr) {
    state->processed_frames += frames;
  }
}

void internalReset(void* instance) {
  auto* state = reinterpret_cast<InternalSamplerState*>(instance);
  if (state != nullptr) {
    state->processed_frames = 0;
  }
}

void internalDestroy(void* /*instance*/) {}

}  // namespace

int main() {
  ff::engine::Engine engine;
  engine.setMasterGain(0.8F);
  engine.setTrackSample(0, std::vector<float>{1.0F, 0.5F, 0.25F});
  engine.startTransport();
  engine.handleMidiNoteOn(36, 127);

  ff::plugin_host::Host plugin_host;
  ff::plugin_host::PluginBinaryInfo internal_binary{};
  internal_binary.sdk_version_major = ff::plugin_host::kSdkVersionMajor;
  internal_binary.sdk_version_minor = ff::plugin_host::kSdkVersionMinor;
  internal_binary.plugin_class = ff::plugin_host::PluginClass::kInstrument;
  internal_binary.entrypoints = ff::plugin_host::PluginEntrypoints{
      .has_create = true,
      .has_prepare = true,
      .has_process = true,
      .has_reset = true,
      .has_destroy = true,
  };
  internal_binary.runtime = ff::plugin_host::PluginRuntimeInfo{
      .rt_safe_process = true,
      .allows_dynamic_allocation = false,
      .requests_process_isolation = false,
      .has_unbounded_cpu_cost = false,
  };

  if (!plugin_host.registerInternalPlugin(
          {"ff.internal.sampler", "Internal Sampler"}, internal_binary,
          ff::plugin_host::PluginLifecycleFns{
              .create = internalCreate,
              .prepare = internalPrepare,
              .process = internalProcess,
              .reset = internalReset,
              .destroy = internalDestroy,
          })) {
    std::cerr << "Failed to register internal SDK plugin\n";
    return 1;
  }

  const char* external_path = std::getenv("FF_DESKTOP_PLUGIN_PATH");
  bool external_loaded_in_process = false;
  std::string external_plugin_id;
  if (external_path != nullptr) {
    const auto load_result = plugin_host.loadPluginBinary(external_path);
    external_plugin_id = load_result.plugin_id;
    external_loaded_in_process =
        load_result.status == ff::plugin_host::LoadStatus::kLoadedInProcess;
    std::cout << "External plugin load status: "
              << static_cast<int>(load_result.status) << " ("
              << load_result.message << ")\n";
  }

  plugin_host.setRoute({.source_id = ff::plugin_host::kRouteHostInput,
                        .destination_id = "ff.internal.sampler",
                        .gain = 1.0F});
  if (external_loaded_in_process) {
    plugin_host.setRoute({.source_id = "ff.internal.sampler",
                          .destination_id = external_plugin_id,
                          .gain = 1.0F});
    plugin_host.setRoute({.source_id = external_plugin_id,
                          .destination_id = ff::plugin_host::kRouteMasterOutput,
                          .gain = 1.0F});
  } else {
    plugin_host.setRoute({.source_id = "ff.internal.sampler",
                          .destination_id = ff::plugin_host::kRouteMasterOutput,
                          .gain = 1.0F});
  }

  plugin_host.activatePlugin("ff.internal.sampler", 48'000.0, 256, 0);
  plugin_host.processPlugin("ff.internal.sampler", 256);
  plugin_host.resetPlugin("ff.internal.sampler");
  plugin_host.deactivatePlugin("ff.internal.sampler");

  std::cout << "Forest Floor desktop host stub\n";
  std::cout << "Registered plugins: " << plugin_host.pluginCount() << "\n";
  std::cout << "Routes: " << plugin_host.routeCount() << "\n";
  std::cout << "Transport running: " << (engine.isTransportRunning() ? "yes" : "no") << "\n";
  return 0;
}

#include "ff/plugin_host/host.hpp"

#include <cstdint>

#if defined(_WIN32)
#define FF_TEST_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define FF_TEST_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

constexpr const char* kPluginId = "ff.test.isolated";
constexpr const char* kPluginName = "Test Isolated Plugin";
int g_instance_state = 0;

}  // namespace

FF_TEST_PLUGIN_EXPORT bool ff_plugin_get_descriptor_v1(
    ff::plugin_host::PluginDescriptorC* out_descriptor) {
  if (out_descriptor == nullptr) {
    return false;
  }

  out_descriptor->id = kPluginId;
  out_descriptor->name = kPluginName;
  return true;
}

FF_TEST_PLUGIN_EXPORT bool ff_plugin_get_binary_info_v1(
    ff::plugin_host::PluginBinaryInfoC* out_binary_info) {
  if (out_binary_info == nullptr) {
    return false;
  }

  out_binary_info->sdk_version_major = ff::plugin_host::kSdkVersionMajor;
  out_binary_info->sdk_version_minor = ff::plugin_host::kSdkVersionMinor;
  out_binary_info->plugin_class = static_cast<std::uint32_t>(ff::plugin_host::PluginClass::kEffect);
  out_binary_info->entrypoints = ff::plugin_host::PluginEntrypointsC{
      .has_create = 1,
      .has_prepare = 1,
      .has_process = 1,
      .has_reset = 1,
      .has_destroy = 1,
  };
  out_binary_info->runtime = ff::plugin_host::PluginRuntimeInfoC{
      .rt_safe_process = 1,
      .allows_dynamic_allocation = 0,
      .requests_process_isolation = 1,
      .has_unbounded_cpu_cost = 1,
  };
  return true;
}

FF_TEST_PLUGIN_EXPORT void* ff_create(void* /*host_context*/) { return &g_instance_state; }
FF_TEST_PLUGIN_EXPORT bool ff_prepare(void* /*instance*/, double /*sample_rate*/,
                                      std::uint32_t /*max_block_size*/,
                                      std::uint32_t /*channel_config*/) {
  return true;
}
FF_TEST_PLUGIN_EXPORT void ff_process(void* /*instance*/, std::uint32_t /*frames*/) {}
FF_TEST_PLUGIN_EXPORT void ff_reset(void* /*instance*/) {}
FF_TEST_PLUGIN_EXPORT void ff_destroy(void* /*instance*/) {}

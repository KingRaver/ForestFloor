#include "ff/plugin_host/host.hpp"

#include <algorithm>
#include <utility>

namespace ff::plugin_host {

bool Host::registerPlugin(PluginDescriptor descriptor) {
  const auto iterator = std::find_if(
      plugins_.begin(), plugins_.end(),
      [&](const PluginDescriptor& existing) { return existing.id == descriptor.id; });
  if (iterator != plugins_.end()) {
    return false;
  }

  plugins_.push_back(std::move(descriptor));
  return true;
}

std::size_t Host::pluginCount() const noexcept { return plugins_.size(); }

}  // namespace ff::plugin_host

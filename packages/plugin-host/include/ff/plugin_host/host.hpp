#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ff::plugin_host {

struct PluginDescriptor final {
  std::string id;
  std::string name;
};

class Host final {
 public:
  bool registerPlugin(PluginDescriptor descriptor);
  [[nodiscard]] std::size_t pluginCount() const noexcept;

 private:
  std::vector<PluginDescriptor> plugins_;
};

}  // namespace ff::plugin_host


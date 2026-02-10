#include <iostream>
#include <string>

#include "ff/engine/engine.hpp"
#include "ff/plugin_host/host.hpp"

int main() {
  ff::engine::Engine engine;
  engine.setMasterGain(0.8F);

  ff::plugin_host::Host plugin_host;
  plugin_host.registerPlugin({"ff.internal.sampler", "Internal Sampler"});

  std::cout << "Forest Floor desktop host stub\n";
  std::cout << "Registered plugins: " << plugin_host.pluginCount() << "\n";
  return 0;
}


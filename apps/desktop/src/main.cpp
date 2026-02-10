#include <iostream>
#include <string>
#include <vector>

#include "ff/engine/engine.hpp"
#include "ff/plugin_host/host.hpp"

int main() {
  ff::engine::Engine engine;
  engine.setMasterGain(0.8F);
  engine.setTrackSample(0, std::vector<float>{1.0F, 0.5F, 0.25F});
  engine.startTransport();
  engine.handleMidiNoteOn(36, 127);

  ff::plugin_host::Host plugin_host;
  plugin_host.registerPlugin({"ff.internal.sampler", "Internal Sampler"});

  std::cout << "Forest Floor desktop host stub\n";
  std::cout << "Registered plugins: " << plugin_host.pluginCount() << "\n";
  std::cout << "Transport running: " << (engine.isTransportRunning() ? "yes" : "no") << "\n";
  return 0;
}

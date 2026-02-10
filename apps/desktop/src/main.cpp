#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "ff/diagnostics/reporter.hpp"

#include "runtime.hpp"

#if defined(APPLE)
#include "macos_ui.hpp"
#endif

namespace {

struct LaunchOptions final {
  bool headless_smoke = false;
  bool headless_soak = false;
};

LaunchOptions parseOptions(int argc, char** argv) {
  LaunchOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--headless-smoke") {
      options.headless_smoke = true;
      continue;
    }
    if (arg == "--headless-soak") {
      options.headless_soak = true;
      continue;
    }
  }

  return options;
}

int runHeadless(ff::desktop::Runtime* runtime,
                ff::diagnostics::Reporter* diagnostics,
                bool soak) {
  if (runtime == nullptr || diagnostics == nullptr) {
    return 1;
  }

  std::string error;
  const std::size_t blocks = soak ? 56'250U : 1'500U;
  const bool ok = runtime->runHeadlessSession(48'000, 256, blocks, &error);
  if (!ok) {
    diagnostics->writeCrashReport(
        "headless_failure",
        error,
        {
            {"mode", soak ? "soak" : "smoke"},
            {"blocks", std::to_string(blocks)},
        });
    std::cerr << "Headless session failed: " << error << "\n";
    return 1;
  }

  const auto status = runtime->status();
  diagnostics->writeRuntimeReport(
      soak ? "desktop_headless_soak" : "desktop_headless_smoke",
      {
          {"blocks", std::to_string(blocks)},
          {"backend_xruns", std::to_string(status.backend_xruns)},
          {"engine_xruns", std::to_string(status.engine_xruns)},
          {"timeline_sample", std::to_string(status.timeline_sample)},
      });

  std::cout << "Headless " << (soak ? "soak" : "smoke")
            << " session completed\n";
  std::cout << "Backend XRuns: " << status.backend_xruns << "\n";
  std::cout << "Engine XRuns: " << status.engine_xruns << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  ff::diagnostics::Reporter diagnostics(
      ff::diagnostics::defaultDiagnosticsDirectory());
  ff::diagnostics::ScopedTerminateHandler terminate_handler(&diagnostics);

  try {
    const LaunchOptions options = parseOptions(argc, argv);

    ff::desktop::Runtime runtime(&diagnostics);

    if (options.headless_smoke || options.headless_soak) {
      return runHeadless(&runtime, &diagnostics, options.headless_soak);
    }

#if defined(APPLE)
    return runMacDesktopApp(&runtime, &diagnostics);
#else
    std::cout << "Forest Floor desktop runtime started in headless mode on this platform.\n";
    std::cout << "Run with --headless-smoke or --headless-soak for CI validation.\n";
    return runHeadless(&runtime, &diagnostics, false);
#endif
  } catch (const std::exception& exception) {
    diagnostics.writeCrashReport(
        "exception",
        exception.what(),
        {
            {"phase", "desktop.main"},
        });
    std::cerr << "Fatal error: " << exception.what() << "\n";
    return 1;
  } catch (...) {
    diagnostics.writeCrashReport(
        "exception",
        "non-standard exception",
        {
            {"phase", "desktop.main"},
        });
    std::cerr << "Fatal error: non-standard exception\n";
    return 1;
  }
}

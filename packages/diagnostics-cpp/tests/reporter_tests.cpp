#include "ff/diagnostics/reporter.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::filesystem::path uniqueTempDir(const std::string& prefix) {
  const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         (prefix + "_" + std::to_string(ticks));
}

std::string readText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.is_open());
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void runtimeReportIncludesExpectedFields() {
  const auto diagnostics_dir = uniqueTempDir("ff_runtime_report_test");
  ff::diagnostics::Reporter reporter(diagnostics_dir);

  std::filesystem::path report_path;
  assert(reporter.writeRuntimeReport(
      "desktop_session",
      {
          {"engine_blocks", "42"},
          {"plugin_count", "2"},
      },
      &report_path));
  assert(std::filesystem::exists(report_path));

  const std::string content = readText(report_path);
  assert(content.find("format_version=1") != std::string::npos);
  assert(content.find("category=runtime") != std::string::npos);
  assert(content.find("name=desktop_session") != std::string::npos);
  assert(content.find("report_type=runtime") != std::string::npos);
  assert(content.find("engine_blocks=42") != std::string::npos);
  assert(content.find("plugin_count=2") != std::string::npos);

  std::error_code cleanup_error;
  std::filesystem::remove_all(diagnostics_dir, cleanup_error);
}

void crashReportIncludesReasonAndMessage() {
  const auto diagnostics_dir = uniqueTempDir("ff_crash_report_test");
  ff::diagnostics::Reporter reporter(diagnostics_dir);

  std::filesystem::path report_path;
  assert(reporter.writeCrashReport(
      "exception",
      "boom",
      {
          {"phase", "desktop.main"},
      },
      &report_path));
  assert(std::filesystem::exists(report_path));

  const std::string content = readText(report_path);
  assert(content.find("category=crash") != std::string::npos);
  assert(content.find("report_type=crash") != std::string::npos);
  assert(content.find("crash_reason=exception") != std::string::npos);
  assert(content.find("crash_message=boom") != std::string::npos);
  assert(content.find("phase=desktop.main") != std::string::npos);

  std::error_code cleanup_error;
  std::filesystem::remove_all(diagnostics_dir, cleanup_error);
}

void defaultDiagnosticsDirectoryUsesEnvironmentOverride() {
#if defined(_WIN32)
  _putenv_s("FF_DIAGNOSTICS_DIR", "C:\\ff_diag_env_test");
  const auto path = ff::diagnostics::defaultDiagnosticsDirectory();
  assert(path.string().find("ff_diag_env_test") != std::string::npos);
  _putenv_s("FF_DIAGNOSTICS_DIR", "");
#else
  setenv("FF_DIAGNOSTICS_DIR", "/tmp/ff_diag_env_test", 1);
  const auto path = ff::diagnostics::defaultDiagnosticsDirectory();
  assert(path == std::filesystem::path("/tmp/ff_diag_env_test"));
  unsetenv("FF_DIAGNOSTICS_DIR");
#endif
}

}  // namespace

int main() {
  runtimeReportIncludesExpectedFields();
  crashReportIncludesReasonAndMessage();
  defaultDiagnosticsDirectoryUsesEnvironmentOverride();
  return 0;
}

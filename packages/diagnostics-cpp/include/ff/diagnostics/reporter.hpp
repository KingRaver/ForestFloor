#pragma once

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ff::diagnostics {

struct ReportField final {
  std::string key;
  std::string value;
};

class Reporter final {
 public:
  explicit Reporter(std::filesystem::path output_directory);

  [[nodiscard]] const std::filesystem::path& outputDirectory() const noexcept;

  bool writeRuntimeReport(std::string_view name,
                          const std::vector<ReportField>& fields,
                          std::filesystem::path* report_path = nullptr) const;
  bool writeCrashReport(std::string_view reason,
                        std::string_view message,
                        const std::vector<ReportField>& fields = {},
                        std::filesystem::path* report_path = nullptr) const;

 private:
  bool writeReport(std::string_view category,
                   std::string_view name,
                   const std::vector<ReportField>& fields,
                   std::filesystem::path* report_path) const;

  std::filesystem::path output_directory_;
};

class ScopedTerminateHandler final {
 public:
  explicit ScopedTerminateHandler(Reporter* reporter) noexcept;
  ~ScopedTerminateHandler() noexcept;

  ScopedTerminateHandler(const ScopedTerminateHandler&) = delete;
  ScopedTerminateHandler& operator=(const ScopedTerminateHandler&) = delete;

 private:
  static void onTerminate() noexcept;

  std::terminate_handler previous_handler_ = nullptr;
  static Reporter* active_reporter_;
};

[[nodiscard]] std::filesystem::path defaultDiagnosticsDirectory();
[[nodiscard]] std::string utcTimestamp();

}  // namespace ff::diagnostics

#include "ff/diagnostics/reporter.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace ff::diagnostics {
namespace {

std::string sanitizeToken(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  for (const char value : input) {
    const bool allowed = (value >= 'a' && value <= 'z') ||
                         (value >= 'A' && value <= 'Z') ||
                         (value >= '0' && value <= '9') ||
                         value == '_' || value == '-' || value == '.';
    output.push_back(allowed ? value : '_');
  }

  if (output.empty()) {
    output = "unknown";
  }
  return output;
}

std::string sanitizeValue(std::string_view input) {
  std::string output(input);
  for (char& value : output) {
    if (value == '\n' || value == '\r') {
      value = ' ';
    }
  }
  return output;
}

std::tm utcTm(std::time_t now) {
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &now);
#else
  gmtime_r(&now, &utc);
#endif
  return utc;
}

std::string compactUtcTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm utc = utcTm(now_time);
  std::ostringstream output;
  output << std::put_time(&utc, "%Y%m%dT%H%M%SZ");
  return output.str();
}

std::uint64_t processId() {
#if defined(_WIN32)
  return static_cast<std::uint64_t>(_getpid());
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

}  // namespace

Reporter* ScopedTerminateHandler::active_reporter_ = nullptr;

Reporter::Reporter(std::filesystem::path output_directory)
    : output_directory_(std::move(output_directory)) {
  if (output_directory_.empty()) {
    output_directory_ = defaultDiagnosticsDirectory();
  }
}

const std::filesystem::path& Reporter::outputDirectory() const noexcept {
  return output_directory_;
}

bool Reporter::writeRuntimeReport(std::string_view name,
                                  const std::vector<ReportField>& fields,
                                  std::filesystem::path* report_path) const {
  std::vector<ReportField> payload;
  payload.reserve(fields.size() + 1);
  payload.push_back({"report_type", "runtime"});
  payload.insert(payload.end(), fields.begin(), fields.end());
  return writeReport("runtime", name, payload, report_path);
}

bool Reporter::writeCrashReport(std::string_view reason,
                                std::string_view message,
                                const std::vector<ReportField>& fields,
                                std::filesystem::path* report_path) const {
  std::vector<ReportField> payload;
  payload.reserve(fields.size() + 2);
  payload.push_back({"report_type", "crash"});
  payload.push_back({"crash_reason", std::string(reason)});
  payload.push_back({"crash_message", std::string(message)});
  payload.insert(payload.end(), fields.begin(), fields.end());
  return writeReport("crash", "crash_report", payload, report_path);
}

bool Reporter::writeReport(std::string_view category,
                           std::string_view name,
                           const std::vector<ReportField>& fields,
                           std::filesystem::path* report_path) const {
  std::error_code create_error;
  std::filesystem::create_directories(output_directory_, create_error);
  if (create_error) {
    return false;
  }

  const std::string category_token = sanitizeToken(category);
  const std::string name_token =
      sanitizeToken(name.empty() ? std::string_view("report") : name);
  const std::string file_name = category_token + "_" + name_token + "_" +
                                compactUtcTimestamp() + "_" +
                                std::to_string(processId()) + ".log";
  const auto output_path = output_directory_ / file_name;

  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }

  output << "format_version=1\n";
  output << "category=" << category_token << "\n";
  output << "name=" << name_token << "\n";
  output << "timestamp_utc=" << utcTimestamp() << "\n";
  output << "pid=" << processId() << "\n";
  for (const auto& field : fields) {
    output << sanitizeToken(field.key) << "=" << sanitizeValue(field.value)
           << "\n";
  }

  if (report_path != nullptr) {
    *report_path = output_path;
  }
  return output.good();
}

ScopedTerminateHandler::ScopedTerminateHandler(Reporter* reporter) noexcept
    : previous_handler_(std::set_terminate(&ScopedTerminateHandler::onTerminate)) {
  active_reporter_ = reporter;
}

ScopedTerminateHandler::~ScopedTerminateHandler() noexcept {
  active_reporter_ = nullptr;
  std::set_terminate(previous_handler_);
}

void ScopedTerminateHandler::onTerminate() noexcept {
  try {
    std::string message = "terminate without active exception";
    const auto current = std::current_exception();
    if (current != nullptr) {
      try {
        std::rethrow_exception(current);
      } catch (const std::exception& exception) {
        message = exception.what();
      } catch (...) {
        message = "non-standard exception";
      }
    }

    if (active_reporter_ != nullptr) {
      active_reporter_->writeCrashReport(
          "terminate",
          message,
          {
              {"handler", "std::terminate"},
          });
    }
  } catch (...) {
  }

  std::abort();
}

std::filesystem::path defaultDiagnosticsDirectory() {
  const char* configured = std::getenv("FF_DIAGNOSTICS_DIR");
  if (configured != nullptr && configured[0] != '\0') {
    return std::filesystem::path(configured);
  }

  std::error_code current_path_error;
  const auto cwd = std::filesystem::current_path(current_path_error);
  if (current_path_error) {
    return std::filesystem::path("diagnostics");
  }

  return cwd / "diagnostics";
}

std::string utcTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm utc = utcTm(now_time);
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

}  // namespace ff::diagnostics

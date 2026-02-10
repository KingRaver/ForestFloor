#include "project_io.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ff::desktop {
namespace {

constexpr char kProjectHeader[] = "FF_PROJECT_V1";
constexpr char kTagBpmPrefix[] = "|FF_BPM=";

std::string encodeText(std::string_view value) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string output;
  output.reserve(value.size() * 2);
  for (unsigned char byte : value) {
    output.push_back(kHex[(byte >> 4) & 0x0F]);
    output.push_back(kHex[byte & 0x0F]);
  }
  return output;
}

bool hexNibble(char value, std::uint8_t* nibble) {
  if (nibble == nullptr) {
    return false;
  }

  if (value >= '0' && value <= '9') {
    *nibble = static_cast<std::uint8_t>(value - '0');
    return true;
  }
  if (value >= 'a' && value <= 'f') {
    *nibble = static_cast<std::uint8_t>(10 + (value - 'a'));
    return true;
  }
  if (value >= 'A' && value <= 'F') {
    *nibble = static_cast<std::uint8_t>(10 + (value - 'A'));
    return true;
  }
  return false;
}

bool decodeText(std::string_view value,
                std::string* output,
                std::string* error_message) {
  if (output == nullptr) {
    if (error_message != nullptr) {
      *error_message = "internal error: decode output is null";
    }
    return false;
  }

  if ((value.size() % 2U) != 0U) {
    if (error_message != nullptr) {
      *error_message = "invalid encoded text length";
    }
    return false;
  }

  output->clear();
  output->reserve(value.size() / 2U);
  for (std::size_t index = 0; index < value.size(); index += 2U) {
    std::uint8_t high = 0;
    std::uint8_t low = 0;
    if (!hexNibble(value[index], &high) || !hexNibble(value[index + 1U], &low)) {
      if (error_message != nullptr) {
        *error_message = "invalid hex text field";
      }
      return false;
    }
    output->push_back(static_cast<char>((high << 4U) | low));
  }

  return true;
}

std::string formatFloat(float value) {
  std::ostringstream stream;
  stream.setf(std::ios::fixed, std::ios::floatfield);
  stream.precision(6);
  stream << value;
  return stream.str();
}

bool parseFloat(std::string_view value, float* output) {
  if (output == nullptr) {
    return false;
  }
  std::string tmp(value);
  char* end = nullptr;
  errno = 0;
  const float parsed = std::strtof(tmp.c_str(), &end);
  if (errno != 0 || end == tmp.c_str() || *end != '\0') {
    return false;
  }
  *output = parsed;
  return true;
}

bool parseInt(std::string_view value, int* output) {
  if (output == nullptr) {
    return false;
  }

  int parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc() || result.ptr != end) {
    return false;
  }

  *output = parsed;
  return true;
}

bool parseUnsigned(std::string_view value, std::size_t* output) {
  if (output == nullptr) {
    return false;
  }

  std::size_t parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc() || result.ptr != end) {
    return false;
  }

  *output = parsed;
  return true;
}

bool splitFields(std::string_view input,
                 char delimiter,
                 std::vector<std::string_view>* output) {
  if (output == nullptr) {
    return false;
  }

  output->clear();
  std::size_t start = 0;
  for (std::size_t index = 0; index <= input.size(); ++index) {
    if (index == input.size() || input[index] == delimiter) {
      output->push_back(input.substr(start, index - start));
      start = index + 1;
    }
  }
  return true;
}

std::string buildProjectNameWithMeta(const ProjectModel& project) {
  std::ostringstream output;
  output.setf(std::ios::fixed, std::ios::floatfield);
  output.precision(6);
  output << project.name << kTagBpmPrefix << project.bpm;
  return output.str();
}

void parseProjectNameAndMeta(const std::string& encoded,
                             ProjectModel* project) {
  if (project == nullptr) {
    return;
  }

  const auto marker = encoded.find(kTagBpmPrefix);
  if (marker == std::string::npos) {
    project->name = encoded;
    project->bpm = 120.0F;
    return;
  }

  project->name = encoded.substr(0, marker);
  const std::string bpm_text = encoded.substr(marker + std::string_view(kTagBpmPrefix).size());
  float parsed_bpm = 120.0F;
  if (parseFloat(bpm_text, &parsed_bpm)) {
    project->bpm = std::clamp(parsed_bpm, 20.0F, 300.0F);
  } else {
    project->bpm = 120.0F;
  }
}

}  // namespace

bool saveProjectToFile(const std::filesystem::path& file_path,
                       const ProjectModel& project,
                       std::string* error_message) {
  std::ofstream output(file_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    if (error_message != nullptr) {
      *error_message = "failed to open project file for write: " + file_path.string();
    }
    return false;
  }

  output << kProjectHeader << "\n";
  output << "name=" << encodeText(buildProjectNameWithMeta(project)) << "\n";
  output << "active_kit=0\n";
  output << "active_pattern=0\n";

  output << "BEGIN_KIT\n";
  output << "name=" << encodeText("Desktop Kit") << "\n";
  for (std::size_t track = 0; track < project.tracks.size(); ++track) {
    const auto& track_state = project.tracks[track];
    if (!track_state.sample_path.empty()) {
      output << "track|" << track << "|" << encodeText(track_state.sample_path) << "\n";
    }

    const int choke = track_state.parameters.choke_group < 0
                          ? -1
                          : std::min(track_state.parameters.choke_group, 15);
    output << "control|" << track << "|"
           << formatFloat(track_state.parameters.gain) << "|"
           << formatFloat(track_state.parameters.pan) << "|"
           << formatFloat(track_state.parameters.filter_cutoff) << "|"
           << formatFloat(track_state.parameters.envelope_decay) << "|"
           << formatFloat(track_state.parameters.pitch_semitones) << "|" << choke
           << "\n";
  }
  output << "END_KIT\n";

  output << "BEGIN_PATTERN\n";
  output << "name=" << encodeText("Desktop Pattern") << "\n";
  output << "swing=" << formatFloat(project.swing) << "\n";
  for (std::size_t track = 0; track < project.pattern.size(); ++track) {
    for (std::size_t step = 0; step < project.pattern[track].size(); ++step) {
      const auto& cell = project.pattern[track][step];
      output << "step|" << track << "|" << step << "|" << (cell.active ? 1 : 0)
             << "|" << static_cast<int>(cell.velocity) << "\n";
    }
  }
  output << "END_PATTERN\n";

  if (!output.good()) {
    if (error_message != nullptr) {
      *error_message = "failed writing project file: " + file_path.string();
    }
    return false;
  }

  return true;
}

bool loadProjectFromFile(const std::filesystem::path& file_path,
                         ProjectModel* project,
                         std::string* error_message) {
  if (project == nullptr) {
    if (error_message != nullptr) {
      *error_message = "internal error: project output is null";
    }
    return false;
  }

  std::ifstream input(file_path, std::ios::binary);
  if (!input.is_open()) {
    if (error_message != nullptr) {
      *error_message = "failed to open project file: " + file_path.string();
    }
    return false;
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }

  if (lines.empty() || lines[0] != kProjectHeader) {
    if (error_message != nullptr) {
      *error_message = "invalid project header";
    }
    return false;
  }

  ProjectModel parsed{};

  std::size_t line_index = 1;
  bool parsed_name = false;
  bool in_kit = false;
  bool in_pattern = false;

  for (; line_index < lines.size(); ++line_index) {
    const std::string_view current(lines[line_index]);
    if (current.empty()) {
      continue;
    }

    if (current == "BEGIN_KIT") {
      in_kit = true;
      continue;
    }
    if (current == "END_KIT") {
      in_kit = false;
      continue;
    }
    if (current == "BEGIN_PATTERN") {
      in_pattern = true;
      continue;
    }
    if (current == "END_PATTERN") {
      in_pattern = false;
      continue;
    }

    if (!in_kit && !in_pattern) {
      if (current.rfind("name=", 0) == 0 && !parsed_name) {
        std::string decoded;
        if (!decodeText(current.substr(5), &decoded, error_message)) {
          return false;
        }
        parseProjectNameAndMeta(decoded, &parsed);
        parsed_name = true;
      }
      continue;
    }

    if (in_kit) {
      if (current.rfind("track|", 0) == 0) {
        std::vector<std::string_view> fields;
        splitFields(current.substr(6), '|', &fields);
        if (fields.size() != 2) {
          if (error_message != nullptr) {
            *error_message = "invalid track line in kit";
          }
          return false;
        }
        std::size_t track = 0;
        if (!parseUnsigned(fields[0], &track) || track >= parsed.tracks.size()) {
          if (error_message != nullptr) {
            *error_message = "track assignment out of range";
          }
          return false;
        }
        std::string sample_path;
        if (!decodeText(fields[1], &sample_path, error_message)) {
          return false;
        }
        parsed.tracks[track].sample_path = sample_path;
        continue;
      }

      if (current.rfind("control|", 0) == 0) {
        std::vector<std::string_view> fields;
        splitFields(current.substr(8), '|', &fields);
        if (fields.size() != 7) {
          if (error_message != nullptr) {
            *error_message = "invalid control line in kit";
          }
          return false;
        }

        std::size_t track = 0;
        if (!parseUnsigned(fields[0], &track) || track >= parsed.tracks.size()) {
          if (error_message != nullptr) {
            *error_message = "control track out of range";
          }
          return false;
        }

        auto& params = parsed.tracks[track].parameters;
        if (!parseFloat(fields[1], &params.gain) ||
            !parseFloat(fields[2], &params.pan) ||
            !parseFloat(fields[3], &params.filter_cutoff) ||
            !parseFloat(fields[4], &params.envelope_decay) ||
            !parseFloat(fields[5], &params.pitch_semitones)) {
          if (error_message != nullptr) {
            *error_message = "invalid control value in kit";
          }
          return false;
        }
        int choke = -1;
        if (!parseInt(fields[6], &choke)) {
          if (error_message != nullptr) {
            *error_message = "invalid choke group value";
          }
          return false;
        }
        params.choke_group = choke;
        continue;
      }

      continue;
    }

    if (in_pattern) {
      if (current.rfind("swing=", 0) == 0) {
        float parsed_swing = 0.0F;
        if (!parseFloat(current.substr(6), &parsed_swing)) {
          if (error_message != nullptr) {
            *error_message = "invalid swing value";
          }
          return false;
        }
        parsed.swing = std::clamp(parsed_swing, 0.0F, 0.45F);
        continue;
      }

      if (current.rfind("step|", 0) == 0) {
        std::vector<std::string_view> fields;
        splitFields(current.substr(5), '|', &fields);
        if (fields.size() != 4) {
          if (error_message != nullptr) {
            *error_message = "invalid step line";
          }
          return false;
        }

        std::size_t track = 0;
        std::size_t step = 0;
        int active = 0;
        int velocity = 0;
        if (!parseUnsigned(fields[0], &track) || !parseUnsigned(fields[1], &step) ||
            !parseInt(fields[2], &active) || !parseInt(fields[3], &velocity)) {
          if (error_message != nullptr) {
            *error_message = "invalid step field";
          }
          return false;
        }

        if (track >= parsed.pattern.size() || step >= parsed.pattern[track].size()) {
          if (error_message != nullptr) {
            *error_message = "step index out of range";
          }
          return false;
        }

        parsed.pattern[track][step].active = (active != 0);
        parsed.pattern[track][step].velocity = static_cast<std::uint8_t>(
            std::clamp(velocity, 0, 127));
        continue;
      }
    }
  }

  if (!parsed_name) {
    parsed.name = "Forest Floor Session";
    parsed.bpm = 120.0F;
  }

  *project = std::move(parsed);
  return true;
}

}  // namespace ff::desktop

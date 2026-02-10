#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "ff/engine/engine.hpp"

namespace ff::desktop {

struct ProjectStep final {
  bool active = false;
  std::uint8_t velocity = 100;
};

struct ProjectTrackState final {
  std::string sample_path;
  ff::engine::TrackParameters parameters;
};

struct ProjectModel final {
  std::string name = "Forest Floor Session";
  float bpm = 120.0F;
  float swing = 0.0F;
  std::array<ProjectTrackState, ff::engine::Engine::kTrackCount> tracks;
  std::array<std::array<ProjectStep, 16>, ff::engine::Engine::kTrackCount> pattern{};
};

bool saveProjectToFile(const std::filesystem::path& file_path,
                       const ProjectModel& project,
                       std::string* error_message = nullptr);

bool loadProjectFromFile(const std::filesystem::path& file_path,
                         ProjectModel* project,
                         std::string* error_message = nullptr);

}  // namespace ff::desktop

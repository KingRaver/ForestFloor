#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ff::desktop {

struct LoadedSample final {
  std::uint32_t source_sample_rate_hz = 0;
  std::vector<float> mono;
};

bool loadMonoSample(const std::filesystem::path& path,
                    std::uint32_t target_sample_rate_hz,
                    LoadedSample* sample,
                    std::string* error_message = nullptr);

}  // namespace ff::desktop

#include "sample_loader.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace ff::desktop {

namespace {

bool readFile(const std::filesystem::path& path,
              std::vector<std::uint8_t>* bytes,
              std::string* error_message) {
  if (bytes == nullptr) {
    if (error_message != nullptr) {
      *error_message = "internal error: bytes output is null";
    }
    return false;
  }

  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input.is_open()) {
    if (error_message != nullptr) {
      *error_message = "failed to open sample file: " + path.string();
    }
    return false;
  }

  const auto size = input.tellg();
  if (size <= 0) {
    if (error_message != nullptr) {
      *error_message = "sample file is empty: " + path.string();
    }
    return false;
  }

  bytes->resize(static_cast<std::size_t>(size));
  input.seekg(0, std::ios::beg);
  input.read(reinterpret_cast<char*>(bytes->data()), size);
  if (!input) {
    if (error_message != nullptr) {
      *error_message = "failed reading sample file bytes: " + path.string();
    }
    return false;
  }

  return true;
}

std::uint16_t le16(const std::uint8_t* bytes) {
  return static_cast<std::uint16_t>(bytes[0] | (static_cast<std::uint16_t>(bytes[1]) << 8));
}

std::uint32_t le32(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0] | (static_cast<std::uint32_t>(bytes[1]) << 8) |
                                    (static_cast<std::uint32_t>(bytes[2]) << 16) |
                                    (static_cast<std::uint32_t>(bytes[3]) << 24));
}

bool isFourCc(const std::uint8_t* bytes,
              char a,
              char b,
              char c,
              char d) noexcept {
  return bytes[0] == static_cast<std::uint8_t>(a) &&
         bytes[1] == static_cast<std::uint8_t>(b) &&
         bytes[2] == static_cast<std::uint8_t>(c) &&
         bytes[3] == static_cast<std::uint8_t>(d);
}

float decodePcm24(const std::uint8_t* bytes) noexcept {
  std::int32_t value =
      static_cast<std::int32_t>(bytes[0]) |
      (static_cast<std::int32_t>(bytes[1]) << 8) |
      (static_cast<std::int32_t>(bytes[2]) << 16);
  if ((value & 0x0080'0000) != 0) {
    value |= static_cast<std::int32_t>(0xFF00'0000);
  }
  return static_cast<float>(value) / 8'388'608.0F;
}

float decodeSample(const std::uint8_t* bytes,
                   std::uint16_t format,
                   std::uint16_t bits_per_sample,
                   std::string* error_message,
                   bool* ok) {
  if (ok == nullptr) {
    return 0.0F;
  }

  *ok = true;
  if (format == 1) {  // PCM
    switch (bits_per_sample) {
      case 8:
        return (static_cast<float>(bytes[0]) - 128.0F) / 128.0F;
      case 16:
        return static_cast<float>(static_cast<std::int16_t>(le16(bytes))) / 32768.0F;
      case 24:
        return decodePcm24(bytes);
      case 32:
        return static_cast<float>(static_cast<std::int32_t>(le32(bytes))) /
               2'147'483'648.0F;
      default:
        break;
    }
  } else if (format == 3) {  // IEEE float
    if (bits_per_sample == 32) {
      float value = 0.0F;
      std::memcpy(&value, bytes, sizeof(float));
      return value;
    }
  }

  if (error_message != nullptr) {
    *error_message = "unsupported WAV sample encoding";
  }
  *ok = false;
  return 0.0F;
}

std::vector<float> resampleLinear(const std::vector<float>& input,
                                  std::uint32_t source_rate_hz,
                                  std::uint32_t target_rate_hz) {
  if (input.empty() || source_rate_hz == 0 || target_rate_hz == 0 ||
      source_rate_hz == target_rate_hz) {
    return input;
  }

  const double ratio = static_cast<double>(source_rate_hz) /
                       static_cast<double>(target_rate_hz);
  const std::size_t output_length = std::max<std::size_t>(
      1,
      static_cast<std::size_t>(std::llround(static_cast<double>(input.size()) /
                                            ratio)));

  std::vector<float> output(output_length, 0.0F);
  for (std::size_t index = 0; index < output_length; ++index) {
    const double source_position = static_cast<double>(index) * ratio;
    const std::size_t lower =
        std::min<std::size_t>(input.size() - 1,
                              static_cast<std::size_t>(source_position));
    const std::size_t upper = std::min<std::size_t>(input.size() - 1, lower + 1);
    const float fraction = static_cast<float>(source_position - static_cast<double>(lower));
    output[index] = input[lower] + ((input[upper] - input[lower]) * fraction);
  }

  return output;
}

}  // namespace

bool loadMonoSample(const std::filesystem::path& path,
                    std::uint32_t target_sample_rate_hz,
                    LoadedSample* sample,
                    std::string* error_message) {
  if (sample == nullptr) {
    if (error_message != nullptr) {
      *error_message = "internal error: sample output is null";
    }
    return false;
  }

  std::vector<std::uint8_t> bytes;
  if (!readFile(path, &bytes, error_message)) {
    return false;
  }

  if (bytes.size() < 44 || !isFourCc(bytes.data(), 'R', 'I', 'F', 'F') ||
      !isFourCc(bytes.data() + 8, 'W', 'A', 'V', 'E')) {
    if (error_message != nullptr) {
      *error_message = "sample must be a RIFF/WAVE file";
    }
    return false;
  }

  std::uint16_t format = 0;
  std::uint16_t channels = 0;
  std::uint16_t bits_per_sample = 0;
  std::uint32_t sample_rate_hz = 0;
  const std::uint8_t* data_ptr = nullptr;
  std::size_t data_size = 0;

  std::size_t offset = 12;
  while (offset + 8 <= bytes.size()) {
    const auto* chunk_header = bytes.data() + offset;
    const std::uint32_t chunk_size = le32(chunk_header + 4);
    const std::size_t payload_start = offset + 8;
    if (payload_start + chunk_size > bytes.size()) {
      if (error_message != nullptr) {
        *error_message = "invalid WAV chunk size";
      }
      return false;
    }

    if (isFourCc(chunk_header, 'f', 'm', 't', ' ')) {
      if (chunk_size < 16) {
        if (error_message != nullptr) {
          *error_message = "invalid WAV fmt chunk";
        }
        return false;
      }
      const auto* fmt = bytes.data() + payload_start;
      format = le16(fmt + 0);
      channels = le16(fmt + 2);
      sample_rate_hz = le32(fmt + 4);
      bits_per_sample = le16(fmt + 14);
    } else if (isFourCc(chunk_header, 'd', 'a', 't', 'a')) {
      data_ptr = bytes.data() + payload_start;
      data_size = chunk_size;
    }

    offset = payload_start + chunk_size + (chunk_size & 1U);
  }

  if (format == 0 || channels == 0 || bits_per_sample == 0 || sample_rate_hz == 0 ||
      data_ptr == nullptr || data_size == 0) {
    if (error_message != nullptr) {
      *error_message = "missing required WAV chunks";
    }
    return false;
  }

  const std::size_t bytes_per_sample = bits_per_sample / 8;
  if (bytes_per_sample == 0) {
    if (error_message != nullptr) {
      *error_message = "invalid bits-per-sample in WAV";
    }
    return false;
  }

  const std::size_t bytes_per_frame = bytes_per_sample * static_cast<std::size_t>(channels);
  if (bytes_per_frame == 0 || data_size < bytes_per_frame) {
    if (error_message != nullptr) {
      *error_message = "invalid WAV frame layout";
    }
    return false;
  }

  const std::size_t frame_count = data_size / bytes_per_frame;
  if (frame_count == 0) {
    if (error_message != nullptr) {
      *error_message = "WAV has no audio frames";
    }
    return false;
  }

  std::vector<float> mono;
  mono.reserve(frame_count);

  for (std::size_t frame = 0; frame < frame_count; ++frame) {
    const auto* frame_data = data_ptr + (frame * bytes_per_frame);
    float mixed = 0.0F;
    for (std::uint16_t channel = 0; channel < channels; ++channel) {
      const auto* sample_ptr = frame_data + (static_cast<std::size_t>(channel) * bytes_per_sample);
      bool decoded_ok = false;
      const float value = decodeSample(sample_ptr, format, bits_per_sample,
                                       error_message, &decoded_ok);
      if (!decoded_ok) {
        return false;
      }
      mixed += value;
    }
    mixed /= static_cast<float>(channels);
    mono.push_back(std::clamp(mixed, -1.0F, 1.0F));
  }

  sample->source_sample_rate_hz = sample_rate_hz;
  sample->mono = resampleLinear(mono, sample_rate_hz,
                                std::max<std::uint32_t>(1, target_sample_rate_hz));

  if (sample->mono.empty()) {
    if (error_message != nullptr) {
      *error_message = "decoded sample is empty";
    }
    return false;
  }

  return true;
}

}  // namespace ff::desktop

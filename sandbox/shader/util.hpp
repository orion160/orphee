#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace shader {
std::vector<uint32_t> load(const std::filesystem::path &path) {
  std::vector<uint32_t> code;
  std::ifstream shader{path, std::ios::binary};

  const auto fileSize = std::filesystem::file_size(path);
  const size_t words = fileSize / sizeof(uint32_t);

  code.resize(words);

  shader.read(reinterpret_cast<char *>(code.data()), fileSize);

  return code;
}
} // namespace shader

#pragma once

#include "diagnostics.hpp"
#include <filesystem>
#include <optional>
#include <string>
namespace smelt {

struct FetchOptions {
  bool update = false;
  std::optional<std::string> name;
};

[[nodiscard]] auto fetchDependencies(const std::filesystem::path &root,
                                     Diagnostics &diags,
                                     const FetchOptions &opts = {}) -> bool;
} // namespace smelt

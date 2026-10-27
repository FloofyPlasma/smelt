#pragma once

#include <filesystem>
namespace smelt {
[[nodiscard]] auto installSelf(const std::filesystem::path &root,
                               const std::filesystem::path &prefix) -> int;

[[nodiscard]] auto uninstallSelf(const std::filesystem::path &prefix) -> int;
} // namespace smelt

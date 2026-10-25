#pragma once

#include <filesystem>
#include <optional>
#include <string>
namespace smelt {
[[nodiscard]] auto checkoutDir(const std::filesystem::path &root,
                               const std::string &name)
    -> std::filesystem::path;

[[nodiscard]] auto headCommit(const std::filesystem::path &dir)
    -> std::optional<std::string>;

[[nodiscard]] auto ensureClone(const std::string &url,
                               const std::filesystem::path &dir) -> bool;

[[nodiscard]] auto resolveTag(const std::filesystem::path &dir,
                              const std::string &tag)
    -> std::optional<std::string>;

[[nodiscard]] auto checkoutCommit(const std::filesystem::path &dir,
                                  const std::string &commit) -> bool;
} // namespace smelt

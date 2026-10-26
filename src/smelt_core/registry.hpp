#pragma once

#include "diagnostics.hpp"
#include <filesystem>
#include <optional>
#include <string>
namespace smelt {
struct RegistryEntry {
  std::string upstreamUrl;
  std::string upstreamCommit;
  std::filesystem::path overlayFile;
};

[[nodiscard]] auto registryDir(const std::filesystem::path &root)
    -> std::filesystem::path;

[[nodiscard]] auto syncRegistry(const std::filesystem::path &root,
                                const std::string &commit, Diagnostics &diags)
    -> std::optional<std::string>;

[[nodiscard]] auto
readRegistryEntry(const std::filesystem::path &regDir, const std::string &name,
                  const std::string &version, Diagnostics &diags)
    -> std::optional<RegistryEntry>;
} // namespace smelt

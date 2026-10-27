#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
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

[[nodiscard]] auto isAllowedGitUrl(const std::string &url) -> bool;

[[nodiscard]] auto isFullCommitHash(const std::string &s) -> bool;

[[nodiscard]] auto originUrl(const std::filesystem::path &dir)
    -> std::optional<std::string>;
[[nodiscard]] auto remoteTagCommit(const std::string &url,
                                   const std::string &tag)
    -> std::optional<std::string>;
[[nodiscard]] auto fetchHistory(const std::string &url,
                                const std::filesystem::path &dir,
                                const std::string &commit) -> bool;
[[nodiscard]] auto checkoutPaths(const std::filesystem::path &dir,
                                 const std::string &commit,
                                 const std::vector<std::string> &paths) -> bool;
} // namespace smelt

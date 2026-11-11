#pragma once

#include "diagnostics.hpp"
#include "sema.hpp"
#include "workspace.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace smelt {
struct SourceFile {
  std::string relPath;
  std::filesystem::path path;
  bool isCxx = false;
};

struct TargetSources {
  std::filesystem::path root;
  std::vector<SourceFile> files;
  std::vector<std::filesystem::path> headers;
};

using SourceMap = std::map<std::string, TargetSources>;

[[nodiscard]] auto currentPlatform() -> std::string_view;

[[nodiscard]] auto platformRules(const TargetInfo &t) -> const PlatformRules *;

[[nodiscard]] auto discoverSources(const Model &model,
                                   const std::filesystem::path &projectRoot,
                                   Diagnostics &diags)
    -> std::optional<SourceMap>;

[[nodiscard]] auto checkOwnership(const std::vector<SourceMap> &sources,
                                  const Workspace &ws, Diagnostics &diags)
    -> bool;
} // namespace smelt

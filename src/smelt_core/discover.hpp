#pragma once

#include "diagnostics.hpp"
#include "sema.hpp"
#include "workspace.hpp"
#include <filesystem>
#include <map>
#include <string>
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
};

using SourceMap = std::map<std::string, TargetSources>;

[[nodiscard]] auto
discoverSources(const Model &model, const std::filesystem::path &projectRoot,
                Diagnostics &diags, std::vector<std::string> &warnings)
    -> std::optional<SourceMap>;

[[nodiscard]] auto checkOwnership(const std::vector<SourceMap> &sources,
                                  const Workspace &ws, Diagnostics &diags)
    -> bool;
} // namespace smelt

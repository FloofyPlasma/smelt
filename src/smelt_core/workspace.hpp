#pragma once

#include "sema.hpp"
#include <cstddef>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
namespace smelt {
using NodeKey = std::pair<std::size_t, std::string>;

struct Package {
  std::string projectFile;
  std::filesystem::path root;
  Model model;
  std::map<std::string, std::size_t> deps;
  std::map<std::string, NodeKey> resolved;
};

struct Workspace {
  std::deque<Package> packages;

  [[nodiscard]] static auto load(const std::filesystem::path &root,
                                 Diagnostics &diags)
      -> std::optional<Workspace>;
};

[[nodiscard]] auto findTarget(const Package &p, const std::string &name)
    -> const TargetInfo *;
} // namespace smelt

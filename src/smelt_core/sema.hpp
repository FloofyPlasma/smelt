#pragma once

#include "ast.hpp"
#include "diagnostics.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
namespace smelt {
enum class Optimization : std::uint8_t { None, Speed, Size };
enum class TargetType : std::uint8_t { Exe, StaticLib };

struct ProjectInfo {
  SourceLoc loc;
  std::string name;
  std::optional<std::string> version;
  std::optional<int> cxxStd;
  std::optional<int> cStd;
};

struct ProfileInfo {
  std::string name;
  Optimization optimization = Optimization::None;
  bool debugInfo = true;
  std::vector<std::string> defines;
};

struct NameRef {
  SourceLoc loc;
  std::string name;
};

struct PlatformRules {
  std::vector<std::string> files;
  std::vector<std::string> exclude;
  std::vector<std::string> link;
};

struct TargetInfo {
  SourceLoc loc;
  std::string name;
  TargetType type = TargetType::Exe;
  std::optional<std::string> sourceDir;
  std::vector<std::string> files;
  std::vector<std::string> exclude;
  bool exported = false;
  std::vector<std::string> publicIncludes;
  std::vector<std::string> privateIncludes;
  std::vector<std::string> defines;
  std::vector<std::string> compileOptions;
  std::vector<std::string> link;
  std::vector<NameRef> depends;
  std::map<std::string, PlatformRules> platforms;
};

struct DependencyInfo {
  SourceLoc loc;
  std::string name;
  std::string path;
  std::optional<std::string> git;
  std::optional<std::string> tag;
  std::optional<std::string> commit;
  std::optional<std::string> registry;
  std::optional<std::string> version;
  std::optional<std::string> target;
};

struct Model {
  ProjectInfo project;
  std::vector<ProfileInfo> profiles;
  std::vector<DependencyInfo> dependencies;
  std::vector<TargetInfo> targets;
};

[[nodiscard]] auto analyze(const ProjectFile &file, Diagnostics &diags)
    -> std::optional<Model>;
} // namespace smelt

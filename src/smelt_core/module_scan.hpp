#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace smelt {
struct ModuleName {
  std::string name;
  std::string sourcePath;
};

struct ScanRule {
  std::vector<ModuleName> provides;
  std::vector<ModuleName> needs;
};

struct ScanUnit {
  std::filesystem::path directory;
  std::filesystem::path file;
  std::filesystem::path output;
  std::vector<std::string> arguments;
};

[[nodiscard]] auto mayUseModules(const std::filesystem::path &file) -> bool;

[[nodiscard]] auto scanModules(const std::vector<ScanUnit> &units,
                               const std::filesystem::path &dbFile,
                               std::map<std::string, ScanRule> &byOutput,
                               std::string &err) -> bool;
} // namespace smelt

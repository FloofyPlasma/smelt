#pragma once

#include <filesystem>
#include <map>
#include <string>
namespace smelt {
struct LockEntry {
  std::string kind;
  std::string url;
  std::string refKind;
  std::string ref;
  std::string commit;
};

struct Lock {
  std::string registryCommit;
  std::map<std::string, LockEntry> entries;
};

[[nodiscard]] auto readLock(const std::filesystem::path &file, Lock &out,
                            std::string &err) -> bool;

[[nodiscard]] auto writeLock(const std::filesystem::path &file,
                             const Lock &lock) -> bool;
} // namespace smelt

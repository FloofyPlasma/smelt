#pragma once

#include <filesystem>
#include <map>
#include <string>
namespace smelt {
struct LockEntry {
  std::string url;
  std::string refKind;
  std::string ref;
  std::string commit;
};

[[nodiscard]] auto readLock(const std::filesystem::path &file,
                            std::map<std::string, LockEntry> &out,
                            std::string &err) -> bool;

[[nodiscard]] auto writeLock(const std::filesystem::path &file,
                             const std::map<std::string, LockEntry> &entries)
    -> bool;
} // namespace smelt

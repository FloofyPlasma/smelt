#include "tools.hpp"
#include <iostream>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>
namespace smelt {
auto findOnPath(const std::string &name) -> std::optional<std::string> {
  if (name.find('/') != std::string::npos) {
    if (::access(name.c_str(), X_OK) == 0)
      return name;
    return std::nullopt;
  }

  const char *path = std::getenv("PATH");
  if (path == nullptr)
    return std::nullopt;

  const std::string all = path;
  std::size_t start = 0;
  while (start <= all.size()) {
    const std::size_t colon = all.find(':', start);
    const std::size_t stop = colon == std::string::npos ? all.size() : colon;
    std::string dir = all.substr(start, stop - start);
    if (dir.empty())
      dir = ".";
    const std::string candidate = dir + "/" + name;
    if (::access(candidate.c_str(), X_OK) == 0)
      return candidate;
    if (colon == std::string::npos)
      break;
    start = colon + 1;
  }
  return std::nullopt;
}

auto requireTools(const std::vector<std::string> &names) -> bool {
  bool ok = true;
  for (const auto &name : names) {
    if (!findOnPath(name)) {
      std::cerr << "smelt: required tool '" << name << "' not found on PATH\n";
      ok = false;
    }
  }
  return ok;
}
} // namespace smelt

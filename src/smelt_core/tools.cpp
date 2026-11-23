#include "tools.hpp"
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#define access _access
#ifndef X_OK
#define X_OK 0
#endif
constexpr char kPathSep = ';';
constexpr char kDirSep = '\\';
#else
#include <unistd.h>
constexpr char kPathSep = ':';
constexpr char kDirSep = '/';
#endif

namespace smelt {
namespace {

auto hasSeparator(const std::string &name) -> bool {
  return name.find('/') != std::string::npos
#ifdef _WIN32
         || name.find('\\') != std::string::npos
#endif
      ;
}

auto isExecutable(const std::string &path) -> bool {
  return ::access(path.c_str(), X_OK) == 0;
}

} // namespace

auto findOnPath(const std::string &name) -> std::optional<std::string> {
  if (hasSeparator(name)) {
    if (isExecutable(name))
      return name;
#ifdef _WIN32
    if (name.find('.') == std::string::npos && isExecutable(name + ".exe"))
      return name + ".exe";
#endif
    return std::nullopt;
  }

  const char *path = std::getenv("PATH");
  if (path == nullptr)
    return std::nullopt;

  const std::string all = path;
  std::size_t start = 0;
  while (start <= all.size()) {
    const std::size_t sep = all.find(kPathSep, start);
    const std::size_t stop = sep == std::string::npos ? all.size() : sep;
    std::string dir = all.substr(start, stop - start);
    if (dir.empty())
      dir = ".";

    const std::string candidate = dir + kDirSep + name;
    if (isExecutable(candidate))
      return candidate;
#ifdef _WIN32
    if (name.find('.') == std::string::npos && isExecutable(candidate + ".exe"))
      return candidate + ".exe";
#endif

    if (sep == std::string::npos)
      break;
    start = sep + 1;
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

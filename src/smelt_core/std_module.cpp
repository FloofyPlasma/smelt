#include "std_module.hpp"
#include "process.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace smelt {
namespace {
auto isFile(const fs::path &p) -> bool {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

// This is only really an edge case on my machine but I'm adding it as a
// function just incase. Im sure someone else out there uses emscripten
// occasionally :p
auto isEmscripten(const fs::path &p) -> bool {
  return p.string().find("emscripten") != std::string::npos;
}
} // namespace

auto clangVersionText() -> std::optional<std::string> {
  return runCapture({"clang++", "--version"}, true);
}

// Fragile.. probably should use regex.. ohwell..
auto clangMajorVersion() -> std::optional<int> {
  const auto text = clangVersionText();
  if (!text)
    return std::nullopt;
  const auto at = text->find("version ");
  if (at == std::string::npos)
    return std::nullopt;
  try {
    return std::stoi(text->substr(at + 8));
  } catch (...) {
    return std::nullopt;
  }
}

auto findStdModule(std::string &err) -> std::optional<fs::path> {
  if (const char *env = std::getenv("SMELT_STD_MODULE"); env && *env) {
    const fs::path p = env;
    if (isFile(p))
      return p;
    err = "SMELT_STD_MODULE is not a file: " + p.string();
    return std::nullopt;
  }

  std::vector<fs::path> candidates = {
      "/usr/share/libc++/v1/std.cppm",
      "/usr/local/share/libc++/v1/std.cppm",
  };

  std::string searched;
  for (const auto &p : candidates) {
    if (isEmscripten(p))
      continue;
    if (isFile(p))
      return p;
    searched += "\n  " + p.string();
  }

  err = "libc++ std module not found; looked in:" + searched +
        "\nset SMELT_STD_MODULE to the path of std.cppm";
  return std::nullopt;
}

} // namespace smelt

#include "git.hpp"
#include "process.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace smelt {
namespace {
auto trim(std::string s) -> std::string {
  while (!s.empty() &&
         (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
    s.pop_back();
  return s;
}
} // namespace

auto checkoutDir(const fs::path &root, const std::string &name) -> fs::path {
  return root / ".smelt" / "git" / name;
}

auto headCommit(const fs::path &dir) -> std::optional<std::string> {
  const auto out = runCapture({"git", "-C", dir.string(), "rev-parse",
                               "--verify", "-q", "HEAD^{commit}"});
  if (!out)
    return std::nullopt;
  return trim(*out);
}

auto ensureClone(const std::string &url, const fs::path &dir) -> bool {
  std::error_code ec;
  if (fs::exists(dir, ec))
    return true;

  fs::create_directories(dir.parent_path());
  std::cout << "  GIT clone " << url << "\n";
  if (runProcess({"git", "clone", "-q", url, dir.string()}) != 0) {
    std::cerr << "smelt: cannot clone '" << url << "'\n";
    fs::remove_all(dir, ec);
    return false;
  }
  return true;
}

auto resolveTag(const fs::path &dir, const std::string &tag)
    -> std::optional<std::string> {
  const auto out = runCapture({"git", "-C", dir.string(), "rev-parse",
                               "--verify", "-q", tag + "^{commit}"});

  if (!out)
    return std::nullopt;
  return trim(*out);
}

auto checkoutCommit(const fs::path &dir, const std::string &commit) -> bool {
  if (headCommit(dir) == commit)
    return true;

  const std::string d = dir.string();

  if (runProcess({"git", "-C", d, "cat-file", "-e", commit + "^{commit}"}) !=
          0 &&
      runProcess({"git", "-C", d, "fetch", "-q", "origin", commit}) != 0) {
    std::cerr << "smelt: commit " << commit << " not found\n";
    return false;
  }

  if (runProcess({"git", "-C", d, "reset", "-q", "--hard", commit}) != 0) {
    std::cerr << "smelt: cannot check out " << commit << "\n";
    return false;
  }
  return headCommit(dir) == commit;
}

} // namespace smelt

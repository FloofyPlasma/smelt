#include "git.hpp"
#include "process.hpp"
#include <algorithm>
#include <cstdlib>
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

auto runGit(const fs::path &dir, const std::vector<std::string> &args) -> bool {
  std::vector<std::string> argv{"git", "-C", dir.string()};
  argv.insert(argv.end(), args.begin(), args.end());

  const std::string log = dir.string() + ".log";
  std::error_code ec;
  if (runProcessLogged(argv, log) == 0) {
    fs::remove(log, ec);
    return true;
  }
  std::cerr << "smelt: git failed; see " << log << "\n";
  return false;
}

auto hasCommit(const fs::path &dir, const std::string &commit) -> bool {
  return runCapture({"git", "-C", dir.string(), "cat-file", "-e",
                     commit + "^{commit}"},
                    true)
      .has_value();
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
  if (fs::exists(dir / ".git", ec))
    return true;

  fs::remove_all(dir, ec);
  fs::create_directories(dir.parent_path(), ec);
  std::cout << "  GIT clone " << url << "\n";

  const std::string log = dir.string() + ".log";
  if (runProcessLogged({"git", "clone", "-q", url, dir.string()}, log) != 0) {
    std::cerr << "smelt: cannot clone '" << url << "'; see " << log << "\n";
    fs::remove_all(dir, ec);
    return false;
  }
  fs::remove(log, ec);
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

auto isAllowedGitUrl(const std::string &url) -> bool {
  if (url.find_first_of(" \t\r\n") != std::string::npos)
    return false;
  if (url.rfind("https://", 0) == 0)
    return true;
  const char *local = std::getenv("SMELT_TEST_LOCAL_GIT");
  return local != nullptr && std::string(local) == "1" &&
         url.rfind("file://", 0) == 0;
}

auto isFullCommitHash(const std::string &s) -> bool {
  return s.size() == 40 && std::all_of(s.begin(), s.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

auto originUrl(const fs::path &dir) -> std::optional<std::string> {
  std::error_code ec;
  if (!fs::exists(dir / ".git", ec))
    return std::nullopt;
  const auto out =
      runCapture({"git", "-C", dir.string(), "remote", "get-url", "origin"});
  if (!out)
    return std::nullopt;
  return trim(*out);
}

auto remoteTagCommit(const std::string &url, const std::string &tag)
    -> std::optional<std::string> {
  const auto out = runCapture({"git", "ls-remote", "--tags", url});
  if (!out)
    return std::nullopt;

  std::optional<std::string> direct, peeled;
  std::istringstream lines(*out);
  std::string line;
  while (std::getline(lines, line)) {
    const auto tab = line.find('\t');
    if (tab == std::string::npos)
      continue;
    const std::string sha = line.substr(0, tab);
    const std::string ref = line.substr(tab + 1);
    if (ref == "refs/tags/" + tag)
      direct = sha;
    else if (ref == "refs/tags/" + tag + "^{}")
      peeled = sha;
  }
  return peeled ? peeled : direct;
}

auto fetchHistory(const std::string &url, const fs::path &dir,
                  const std::string &commit) -> bool {
  std::error_code ec;
  if (!fs::exists(dir / ".git", ec)) {
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    if (!runGit(dir, {"init", "-q"}) ||
        !runGit(dir, {"remote", "add", "origin", url}))
      return false;
  }

  if (hasCommit(dir, commit))
    return true;

  std::cout << "  GIT fetch " << url << " at " << commit.substr(0, 12) << "\n";
  if (!runGit(dir, {"fetch", "-q", "--depth", "1", "--filter=blob:none",
                    "origin", commit})) {
    std::cerr << "smelt: " << url << " does not serve commit " << commit
              << " directly\n";
    return false;
  }
  return true;
}

auto checkoutPaths(const fs::path &dir, const std::string &commit,
                   const std::vector<std::string> &paths) -> bool {
  std::error_code ec;
  if (paths.empty()) {
    if (fs::exists(dir / ".git" / "info" / "sparse-checkout", ec) &&
        !runGit(dir, {"sparse-checkout", "disable"}))
      return false;
  } else {
    std::vector<std::string> set = {"sparse-checkout", "set", "--no-cone"};
    set.insert(set.end(), paths.begin(), paths.end());
    if (!runGit(dir, set))
      return false;
  }

  if (!runGit(dir, {"reset", "-q", "--hard", commit}))
    return false;
  return headCommit(dir) == commit;
}
} // namespace smelt

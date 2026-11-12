#include "install.hpp"
#include "diagnostics.hpp"
#include "process.hpp"
#include "workspace.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace smelt {
namespace {

#ifdef _WIN32
constexpr const char *kBinaryName = "smelt.exe";
constexpr char kPathSep = ';';
#else
constexpr const char *kBinaryName = "smelt";
constexpr char kPathSep = ':';
#endif

auto runSmelt(const fs::path &binary, const fs::path &dir,
              const std::vector<std::string> &args) -> bool {
  std::error_code ec;
  const fs::path before = fs::current_path(ec);
  fs::current_path(dir, ec);
  if (ec) {
    std::cerr << "smelt: cannot enter " << dir.string() << "\n";
    return false;
  }

  std::vector<std::string> argv{binary.string()};
  argv.insert(argv.end(), args.begin(), args.end());
  const int rc = runProcess(argv);

  fs::current_path(before, ec);
  return rc == 0;
}

auto onPath(const fs::path &dir) -> bool {
  const char *path = std::getenv("PATH");
  if (path == nullptr)
    return false;
  const std::string all = path;
  std::size_t start = 0;
  while (start <= all.size()) {
    const std::size_t sep = all.find(kPathSep, start);
    const std::size_t stop = sep == std::string::npos ? all.size() : sep;
    if (all.substr(start, stop - start) == dir.string())
      return true;
    if (sep == std::string::npos)
      break;
    start = sep + 1;
  }
  return false;
}

auto sameContents(const fs::path &a, const fs::path &b) -> bool {
  std::error_code ec;
  const auto sizeA = fs::file_size(a, ec);
  if (ec)
    return false;
  const auto sizeB = fs::file_size(b, ec);
  if (ec || sizeA != sizeB)
    return false;

  std::ifstream fa(a, std::ios::binary);
  std::ifstream fb(b, std::ios::binary);
  if (!fa || !fb)
    return false;

  std::istreambuf_iterator<char> ia(fa), ib(fb), end;
  return std::equal(ia, end, ib);
}

auto getExecutablePath() -> fs::path {
#ifdef _WIN32
  char buffer[MAX_PATH];
  const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
  if (len == 0 || len >= MAX_PATH)
    return {};
  return fs::path(std::string(buffer, len));

#elif defined(__linux__)
  std::error_code ec;
  auto path = fs::read_symlink("/proc/self/exe", ec);
  if (ec)
    return {};
  return path;

#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);

  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0)
    return {};

  return fs::canonical(buffer);

#else
#error "Unsupported platform"
#endif
}
} // namespace

auto installSelf(const fs::path &root, const std::filesystem::path &prefix)
    -> int {
  std::cout << "==> smelt install: prefix " << prefix.string() << "\n";
  Diagnostics diags((root / "project.smelt").string());
  const auto model = loadModel(root / "project.smelt", diags);
  if (!model) {
    for (const auto &d : diags.items())
      std::cerr << Diagnostics::format(d) << "\n";
    return 1;
  }
  if (model->project.name != "smelt") {
    std::cerr << "smelt: install must run from the Smelt source tree\n";
    return 1;
  }

  std::error_code ec;
  const fs::path self = getExecutablePath();
  if (self.empty()) {
    std::cerr << "smelt: cannot find the running binary\n";
    return 1;
  }

  const fs::path work = root / "build" / "install";
  fs::remove_all(work, ec);
  fs::create_directories(work, ec);

  std::cout << "==> build   release profile, stage A (running binary)\n";

  if (!runSmelt(self, root, {"build", "--profile", "release"})) {
    std::cerr << "smelt: release build failed\n";
    return 1;
  }
  const fs::path releaseBinary =
      root / "build" / "release" / "smelt" / kBinaryName;
  const fs::path first = work / (std::string("smelt-a") +
#ifdef _WIN32
                                 ".exe"
#else
                                 ""
#endif
                                );
  fs::copy_file(releaseBinary, first, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    std::cerr << "smelt: cannot copy the release binary\n";
    return 1;
  }

  fs::remove_all(root / "build" / "release", ec);

  std::cout << "==> build   release profile, stage B (clean build, verify "
               "reproduction)\n";

  if (!runSmelt(first, root, {"build", "--profile", "release"})) {
    std::cerr << "smelt: self-build failed\n";
    return 1;
  }
  const fs::path second = releaseBinary;

  std::cout << "==> check   stage A and stage B are identical\n";

  if (!sameContents(first, second)) {
    std::cerr
        << "smelt: release binary does not reproduce itself; not installing\n";
    return 1;
  }

  std::cout << "==> ok      stage A and stage B are identical\n";

  const fs::path bin = prefix / "bin";
  const fs::path target = bin / kBinaryName;
  const fs::path tmp = bin / (std::string(kBinaryName) + ".tmp");
  fs::create_directories(bin, ec);

  std::cout << "==> copy    " << tmp.string() << "\n";

  fs::copy_file(second, tmp, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    std::cerr << "smelt: cannot write " << tmp.string() << ": " << ec.message()
              << "\n";
    return 1;
  }

#ifndef _WIN32
  fs::permissions(tmp,
                  fs::perms::owner_all | fs::perms::group_read |
                      fs::perms::group_exec | fs::perms::others_read |
                      fs::perms::others_exec,
                  ec);
#endif

  std::cout << "==> rename  " << tmp.string() << " -> " << target.string()
            << "\n";

  fs::rename(tmp, target, ec);
  if (ec) {
    std::cerr << "smelt: cannot install to " << target.string() << ": "
              << ec.message() << "\n";
    return 1;
  }

  std::cout << "installed " << target.string() << "\n";

#ifndef _WIN32
  if (const auto sum = runCapture({"sha256sum", target.string()})) {
    std::cout << "sha256 " << sum->substr(0, sum->find(' ')) << "\n";
  }
#endif

  if (!onPath(bin)) {
    std::cout << "note: " << bin.string()
              << " is not on PATH; add it to your PATH setting\n";
  }
  return 0;
}

auto uninstallSelf(const fs::path &prefix) -> int {
  const fs::path target = prefix / "bin" / kBinaryName;
  std::error_code ec;
  if (!fs::exists(target, ec)) {
    std::cout << "nothing to remove at " << target.string() << "\n";
    return 0;
  }
  fs::remove(target, ec);
  if (ec) {
    std::cerr << "smelt: cannot remove " << target.string() << ": "
              << ec.message() << "\n";
    return 1;
  }
  std::cout << "removed " << target.string() << "\n";
  return 0;
}

} // namespace smelt

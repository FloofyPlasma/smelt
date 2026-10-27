#include "install.hpp"
#include "diagnostics.hpp"
#include "process.hpp"
#include "workspace.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace smelt {
namespace {
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
    const std::size_t colon = all.find(':', start);
    const std::size_t stop = colon == std::string::npos ? all.size() : colon;
    if (all.substr(start, stop - start) == dir.string())
      return true;
    if (colon == std::string::npos)
      break;
    start = colon + 1;
  }
  return false;
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
  const fs::path self = fs::read_symlink("/proc/self/exe", ec);
  if (ec) {
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
  const fs::path first = work / "smelt-a";
  fs::copy_file(root / "build" / "release" / "smelt" / "smelt", first,
                fs::copy_options::overwrite_existing, ec);
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
  const fs::path second = root / "build" / "release" / "smelt" / "smelt";

  std::cout << "==> check   stage A and stage B are identical\n";

  if (runProcess({"cmp", "-s", first.string(), second.string()}) != 0) {
    std::cerr
        << "smelt: release binary does not reproduce itself; not installing\n";
    return 1;
  }

  std::cout << "==> ok      stage A and stage B are identical\n";

  const fs::path bin = prefix / "bin";
  const fs::path target = bin / "smelt";
  const fs::path tmp = bin / "smelt.tmp";
  fs::create_directories(bin, ec);

  std::cout << "==> copy    " << tmp.string() << "\n";

  fs::copy_file(second, tmp, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    std::cerr << "smelt: cannot write " << tmp.string() << ": " << ec.message()
              << "\n";
    return 1;
  }
  fs::permissions(tmp,
                  fs::perms::owner_all | fs::perms::group_read |
                      fs::perms::group_exec | fs::perms::others_read |
                      fs::perms::others_exec,
                  ec);

  std::cout << "==> rename  " << tmp.string() << " -> " << target.string()
            << "\n";

  fs::rename(tmp, target, ec);
  if (ec) {
    std::cerr << "smelt: cannot install to " << target.string() << ": "
              << ec.message() << "\n";
    return 1;
  }

  std::cout << "installed " << target.string() << "\n";
  if (const auto sum = runCapture({"sha256sum", target.string()})) {
    std::cout << "sha256 " << sum->substr(0, sum->find(' ')) << "\n";
  }
  if (!onPath(bin)) {
    std::cout << "note: " << bin.string()
              << " is not on PATH; add it to your shell profile\n";
  }
  return 0;
}

auto uninstallSelf(const fs::path &prefix) -> int {
  const fs::path target = prefix / "bin" / "smelt";
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

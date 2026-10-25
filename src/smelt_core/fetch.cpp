#include "fetch.hpp"
#include "diagnostics.hpp"
#include "git.hpp"
#include "lock.hpp"
#include "sema.hpp"
#include "workspace.hpp"
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace smelt {
namespace {
class Fetcher {
public:
  Fetcher(fs::path root, Diagnostics &diags)
      : root_(std::move(root)), diags_(diags) {}

  auto run() -> bool {
    std::string err;
    if (!readLock(root_ / "smelt.lock", existing_, err)) {
      std::cerr << "smelt: " << err << "\n";
      return false;
    }

    std::vector<fs::path> queue{root_};
    std::set<fs::path> visited;
    while (!queue.empty()) {
      const fs::path dir = queue.back();
      queue.pop_back();

      std::error_code ec;
      const fs::path key = fs::weakly_canonical(dir, ec);
      if (!visited.insert(ec ? dir : key).second)
        continue;

      const auto model = loadModel(dir, diags_);
      if (!model)
        return false;

      diags_.setFile((dir / "project.smelt").string());
      for (const auto &dep : model->dependencies) {
        if (dep.git) {
          if (!fetchGit(dep))
            return false;
          queue.push_back(checkoutDir(root_, dep.name));
        } else {
          queue.push_back(dir / dep.path);
        }
      }
    }

    if (newEntries_.empty())
      return true;

    std::map<std::string, LockEntry> merged = existing_;
    for (const auto &[name, entry] : newEntries_)
      merged[name] = entry;

    if (!writeLock(root_ / "smelt.lock", merged)) {
      std::cerr << "smelt: cannot write smelt.lock\n";
      return false;
    }
    std::cout << "write smelt.lock (" << newEntries_.size()
              << " new entries)\n";
    return true;
  }

private:
  auto fetchGit(const DependencyInfo &dep) -> bool {
    LockEntry want;
    want.url = *dep.git;
    want.refKind = dep.tag ? "tag" : "commit";
    want.ref = dep.tag ? *dep.tag : *dep.commit;
    const fs::path dir = checkoutDir(root_, dep.name);

    const LockEntry *known = nullptr;
    if (const auto it = existing_.find(dep.name); it != existing_.end())
      known = &it->second;
    else if (const auto it2 = newEntries_.find(dep.name);
             it2 != newEntries_.end())
      known = &it2->second;

    if (known) {
      if (known->url != want.url || known->refKind != want.refKind ||
          known->ref != want.ref) {
        diags_.error(
            dep.loc,
            "dependency '" + dep.name +
                "' does not match smelt.lock; changing a lcoked dependency "
                "needs 'smelt update', which is not available yet");
        return false;
      }
      if (!ensureClone(known->url, dir) ||
          !checkoutCommit(dir, known->commit)) {
        diags_.error(dep.loc, "cannot restore to '" + dep.name + "' at " +
                                  known->commit);
        return false;
      }
      return true;
    }

    if (!ensureClone(want.url, dir)) {
      diags_.error(dep.loc, "cannot fetch '" + dep.name + "' from " + want.url);
      return false;
    }

    const std::optional<std::string> commit =
        dep.tag ? resolveTag(dir, *dep.tag)
                : std::optional<std::string>(*dep.commit);
    if (!commit) {
      diags_.error(dep.loc, "tag '" + *dep.tag + "' not found in " + want.url);
      return false;
    }
    if (!checkoutCommit(dir, *commit)) {
      diags_.error(dep.loc,
                   "cannot check out " + *commit + " for '" + dep.name + "'");
      return false;
    }

    want.commit = *commit;
    newEntries_[dep.name] = want;
    return true;
  }

  fs::path root_;
  Diagnostics &diags_;
  std::map<std::string, LockEntry> existing_;
  std::map<std::string, LockEntry> newEntries_;
};
} // namespace

auto fetchDependencies(const fs::path &root, Diagnostics &diags) -> bool {
  Fetcher fetcher(root, diags);
  return fetcher.run();
}

} // namespace smelt

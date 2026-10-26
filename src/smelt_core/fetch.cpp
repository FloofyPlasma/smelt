#include "fetch.hpp"
#include "git.hpp"
#include "lock.hpp"
#include "registry.hpp"
#include "workspace.hpp"
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace smelt {
namespace {

struct Visit {
  fs::path file;
  fs::path root;
};

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

    std::vector<Visit> queue{{root_ / "project.smelt", root_}};
    std::set<fs::path> visited;

    while (!queue.empty()) {
      const Visit v = queue.back();
      queue.pop_back();

      std::error_code ec;
      const fs::path key = fs::weakly_canonical(v.root, ec);
      if (!visited.insert(ec ? v.root : key).second)
        continue;

      const auto model = loadModel(v.file, diags_);
      if (!model)
        return false;

      for (const auto &dep : model->dependencies) {
        diags_.setFile(v.file.string());
        if (dep.git) {
          if (!fetchGit(dep))
            return false;
          const fs::path dir = checkoutDir(root_, dep.name);
          queue.push_back({dir / "project.smelt", dir});
        } else if (dep.registry) {
          const auto overlay = fetchRegistry(dep);
          if (!overlay)
            return false;
          queue.push_back({*overlay, checkoutDir(root_, dep.name)});
        } else {
          const fs::path dir = v.root / dep.path;
          queue.push_back({dir / "project.smelt", dir});
        }
      }
    }

    return writeIfChanged();
  }

private:
  auto lookup(const std::string &name) const -> const LockEntry * {
    if (auto it = existing_.entries.find(name); it != existing_.entries.end())
      return &it->second;
    if (auto it = newEntries_.find(name); it != newEntries_.end())
      return &it->second;
    return nullptr;
  }

  auto fetchGit(const DependencyInfo &dep) -> bool {
    LockEntry want;
    want.kind = "git";
    want.url = *dep.git;
    want.refKind = dep.tag ? "tag" : "commit";
    want.ref = dep.tag ? *dep.tag : *dep.commit;
    const fs::path dir = checkoutDir(root_, dep.name);

    if (const LockEntry *known = lookup(dep.name)) {
      if (known->kind != want.kind || known->url != want.url ||
          known->refKind != want.refKind || known->ref != want.ref) {
        diags_.error(dep.loc, "dependency '" + dep.name +
                                  "' does not match smelt.lock; changing a "
                                  "locked dependency needs 'smelt update', "
                                  "which is not available yet");
        return false;
      }
      if (!ensureClone(known->url, dir) ||
          !checkoutCommit(dir, known->commit)) {
        diags_.error(dep.loc,
                     "cannot restore '" + dep.name + "' at " + known->commit);
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

  auto fetchRegistry(const DependencyInfo &dep) -> std::optional<fs::path> {
    if (!registryReady_) {
      const auto c = syncRegistry(root_, existing_.registryCommit, diags_);
      if (!c)
        return std::nullopt;
      regCommit_ = *c;
      registryReady_ = true;
    }

    const auto entry =
        readRegistryEntry(registryDir(root_), dep.name, *dep.version, diags_);
    if (!entry)
      return std::nullopt;

    LockEntry want;
    want.kind = "registry";
    want.url = entry->upstreamUrl;
    want.refKind = "version";
    want.ref = *dep.version;
    want.commit = entry->upstreamCommit;

    if (const LockEntry *known = lookup(dep.name)) {
      if (known->kind != want.kind || known->url != want.url ||
          known->ref != want.ref || known->commit != want.commit) {
        diags_.error(dep.loc, "registry entry for '" + dep.name +
                                  "' does not match smelt.lock; changing a "
                                  "locked dependency needs 'smelt update', "
                                  "which is not available yet");
        return std::nullopt;
      }
    } else {
      newEntries_[dep.name] = want;
    }

    const fs::path dir = checkoutDir(root_, dep.name);
    if (!ensureClone(want.url, dir) || !checkoutCommit(dir, want.commit)) {
      diags_.error(dep.loc,
                   "cannot fetch '" + dep.name + "' at " + want.commit);
      return std::nullopt;
    }
    return entry->overlayFile;
  }

  auto writeIfChanged() -> bool {
    const bool registryChanged =
        registryReady_ && regCommit_ != existing_.registryCommit;
    if (newEntries_.empty() && !registryChanged)
      return true;

    Lock merged;
    merged.registryCommit =
        registryReady_ ? regCommit_ : existing_.registryCommit;
    merged.entries = existing_.entries;
    for (const auto &[name, e] : newEntries_)
      merged.entries[name] = e;

    if (!writeLock(root_ / "smelt.lock", merged)) {
      std::cerr << "smelt: cannot write smelt.lock\n";
      return false;
    }
    std::cout << "wrote smelt.lock (" << newEntries_.size()
              << " new entries)\n";
    return true;
  }

  fs::path root_;
  Diagnostics &diags_;
  Lock existing_;
  std::map<std::string, LockEntry> newEntries_;
  std::string regCommit_;
  bool registryReady_ = false;
};

} // namespace

auto fetchDependencies(const fs::path &root, Diagnostics &diags) -> bool {
  Fetcher fetcher(root, diags);
  return fetcher.run();
}

} // namespace smelt

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
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace smelt {
namespace {

struct Visit {
  fs::path file;
  fs::path root;
};

auto sameDeclaration(const LockEntry &a, const LockEntry &b) -> bool {
  return a.kind == b.kind && a.url == b.url && a.refKind == b.refKind &&
         a.ref == b.ref;
}

class Fetcher {
public:
  Fetcher(fs::path root, Diagnostics &diags, FetchOptions opts)
      : root_(std::move(root)), diags_(diags), opts_(std::move(opts)) {}

  auto run() -> bool {
    std::string err;
    if (!readLock(root_ / "smelt.lock", existing_, err)) {
      std::cerr << "smelt: " << err << "\n";
      return false;
    }

    if (opts_.name && !opts_.update)
      opts_.name.reset();

    if (opts_.name && !existing_.entries.count(*opts_.name)) {
      diags_.error({}, "'" + *opts_.name +
                           "' is not in smelt.lock; run 'smelt fetch' first");
      return false;
    }

    if (registryMoves()) {
      const auto c = syncRegistry(root_, "", diags_);
      if (!c)
        return false;
      regCommit_ = *c;
      registryReady_ = true;
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
        if (opts_.name && dep.name == *opts_.name)
          sawName_ = true;

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

    if (opts_.name && !sawName_) {
      diags_.error({},
                   "no dependency named '" + *opts_.name + "' in this project");
      return false;
    }

    return writeIfChanged();
  }

private:
  auto inScope(const std::string &name) const -> bool {
    return opts_.update && (!opts_.name || *opts_.name == name);
  }

  auto registryMoves() const -> bool {
    for (const auto &[name, e] : existing_.entries) {
      if (e.kind == "registry" && inScope(name))
        return true;
    }
    return false;
  }

  auto restore(SourceLoc loc, const std::string &name, const std::string &url,
               const fs::path &dir, const std::string &commit) -> bool {
    if (!ensureClone(url, dir) || !checkoutCommit(dir, commit)) {
      diags_.error(loc, "cannot restore '" + name + "' at " + commit);
      return false;
    }
    return true;
  }

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

    if (const auto it = newEntries_.find(dep.name); it != newEntries_.end()) {
      if (!sameDeclaration(it->second, want)) {
        diags_.error(dep.loc, "dependency '" + dep.name +
                                  "' is declared differently in two projects");
        return false;
      }
      return restore(dep.loc, dep.name, want.url, dir, it->second.commit);
    }

    if (inScope(dep.name)) {
      std::error_code ec;
      fs::remove_all(dir, ec);
    } else if (const auto it = existing_.entries.find(dep.name);
               it != existing_.entries.end()) {
      if (!sameDeclaration(it->second, want)) {
        diags_.error(dep.loc,
                     "dependency '" + dep.name +
                         "' does not match smelt.lock; run 'smelt update " +
                         dep.name + "' to move it");
        return false;
      }
      return restore(dep.loc, dep.name, want.url, dir, it->second.commit);
    }

    if (!ensureClone(want.url, dir)) {
      diags_.error(dep.loc, "cannot fetch '" + dep.name + "' from " + want.url);
      return false;
    }

    const std::optional<std::string> commit =
        dep.tag ? resolveTag(dir, *dep.tag)
                : std::optional<std::string>(*dep.commit);
    if (!commit) {
      diags_.error(dep.loc, "cannot resolve '" + dep.name + "' in " + want.url);
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
    const fs::path dir = checkoutDir(root_, dep.name);

    if (const auto it = newEntries_.find(dep.name); it != newEntries_.end()) {
      if (!sameDeclaration(it->second, want)) {
        diags_.error(dep.loc, "dependency '" + dep.name +
                                  "' is declared differently in two projects");
        return std::nullopt;
      }
      if (!restore(dep.loc, dep.name, want.url, dir, want.commit))
        return std::nullopt;
      return entry->overlayFile;
    }

    if (inScope(dep.name)) {
      std::error_code ec;
      fs::remove_all(dir, ec);
    } else if (const auto it = existing_.entries.find(dep.name);
               it != existing_.entries.end()) {
      if (!sameDeclaration(it->second, want) ||
          it->second.commit != want.commit) {
        diags_.error(dep.loc,
                     "registry entry for '" + dep.name +
                         "' differs from smelt.lock; run 'smelt update " +
                         dep.name + "' to move it");
        return std::nullopt;
      }
      if (!restore(dep.loc, dep.name, want.url, dir, want.commit))
        return std::nullopt;
      return entry->overlayFile;
    }

    newEntries_[dep.name] = want;
    if (!restore(dep.loc, dep.name, want.url, dir, want.commit))
      return std::nullopt;
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

    for (const auto &[name, e] : newEntries_) {
      const auto old = existing_.entries.find(name);
      if (old == existing_.entries.end())
        std::cout << "  locked " << name << " at " << e.commit << "\n";
      else if (old->second.commit != e.commit || old->second.ref != e.ref)
        std::cout << "  updated " << name << " to " << e.commit << "\n";
      merged.entries[name] = e;
    }

    if (!writeLock(root_ / "smelt.lock", merged)) {
      std::cerr << "smelt: cannot write smelt.lock\n";
      return false;
    }

    std::cout << "wrote smelt.lock\n";
    return true;
  }

  fs::path root_;
  Diagnostics &diags_;
  FetchOptions opts_;
  Lock existing_;
  std::map<std::string, LockEntry> newEntries_;
  std::string regCommit_;
  bool registryReady_ = false;
  bool sawName_ = false;
};

} // namespace

auto fetchDependencies(const fs::path &root, Diagnostics &diags,
                       const FetchOptions &opts) -> bool {
  Fetcher fetcher(root, diags, opts);
  return fetcher.run();
}

} // namespace smelt

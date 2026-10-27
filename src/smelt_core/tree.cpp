#include "tree.hpp"
#include "git.hpp"
#include "lock.hpp"
#include "process.hpp"
#include "registry.hpp"
#include "sema.hpp"
#include "tools.hpp"
#include "workspace.hpp"
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace fs = std::filesystem;

namespace smelt {
namespace {
struct Project {
  std::string label;
  fs::path root;
  std::optional<Model> model;
  std::string missing;
  std::map<std::string, std::size_t> deps;
};

struct Location {
  fs::path root;
  fs::path file;
  std::string missing;
};

auto findTargetIn(const Model &m, const std::string &name)
    -> const TargetInfo * {
  for (const auto &t : m.targets) {
    if (t.name == name)
      return &t;
  }
  return nullptr;
}

auto findDependencyIn(const Model &m, const std::string &name)
    -> const DependencyInfo * {
  for (const auto &d : m.dependencies) {
    if (d.name == name)
      return &d;
  }
  return nullptr;
}

auto kindOf(const TargetInfo &t) -> std::string {
  return t.type == TargetType::Exe ? "exe" : "lib";
}

auto firstError(const Diagnostics &d) -> std::string {
  if (d.items().empty())
    return "invalid project file";
  return "invalid project file: " + d.items().front().message;
}

auto trimmed(std::string s) -> std::string {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.pop_back();
  return s;
}

class Tree {
public:
  explicit Tree(fs::path root) : rootDir_(std::move(root)) {}

  auto run() -> int {
    std::string err;
    if (!readLock(rootDir_ / "smelt.lock", lock_, err)) {
      std::cerr << "smelt: " << err << "\n";
      return 1;
    }

    Diagnostics diags((rootDir_ / "project.smelt").string());
    auto model = loadModel(rootDir_ / "project.smelt", diags);
    if (!model) {
      for (const auto &d : diags.items())
        std::cerr << Diagnostics::format(d) << "\n";
      return 1;
    }

    Project rootProject;
    rootProject.label = model->project.name;
    rootProject.root = rootDir_;
    rootProject.model = std::move(model);
    projects_.push_back(std::move(rootProject));
    byRoot_[rootDir_] = 0;

    for (std::size_t i = 0; i < projects_.size(); ++i) {
      if (!projects_[i].model)
        continue;
      const std::vector<DependencyInfo> deps = projects_[i].model->dependencies;
      const fs::path parent = projects_[i].root;
      for (const auto &dep : deps) {
        const std::size_t idx = resolve(dep, parent);
        projects_[i].deps[dep.name] = idx;
      }
    }

    printRoot();
    return 0;
  }

private:
  auto locate(const DependencyInfo &dep, const fs::path &parent) const
      -> Location {
    Location out;
    std::error_code ec;

    if (!dep.path.empty()) {
      const fs::path dir = parent / dep.path;
      if (!fs::exists(dir / "project.smelt", ec)) {
        out.missing = "no project.smelt at '" + dep.path + "'";
        return out;
      }
      out.root = fs::canonical(dir, ec);
      out.file = out.root / "project.smelt";
      return out;
    }

    if (!dep.pkgConfig.empty()) {
      out.missing = "";
      return out;
    }

    if (!lock_.entries.count(dep.name)) {
      out.missing = "not in smelt.lock; run 'smelt fetch'";
      return out;
    }
    const fs::path dir = checkoutDir(rootDir_, dep.name);
    if (!fs::exists(dir, ec)) {
      out.missing = "not fetched; run 'smelt fetch'";
      return out;
    }

    if (dep.registry) {
      const fs::path reg = registryDir(rootDir_);
      if (!fs::exists(reg, ec)) {
        out.missing = "registry not fetched; run 'smelt fetch'";
        return out;
      }
      Diagnostics quiet("");
      const auto entry = readRegistryEntry(reg, dep.name, *dep.version, quiet);
      if (!entry) {
        out.missing = "no registry entry for version " + *dep.version;
        return out;
      }
      out.root = fs::canonical(dir, ec);
      out.file = entry->overlayFile;
      return out;
    }

    out.root = fs::canonical(dir, ec);
    out.file = out.root / "project.smelt";
    return out;
  }

  auto resolve(const DependencyInfo &dep, const fs::path &parent)
      -> std::size_t {
    if (!dep.pkgConfig.empty())
      return resolvePkgConfig(dep);

    const Location loc = locate(dep, parent);
    if (!loc.missing.empty()) {
      Project p;
      p.label = dep.name;
      p.missing = loc.missing;
      projects_.push_back(std::move(p));
      return projects_.size() - 1;
    }

    if (const auto it = byRoot_.find(loc.root); it != byRoot_.end())
      return it->second;

    Diagnostics diags(loc.file.string());
    Project p;
    p.root = loc.root;
    p.model = loadModel(loc.file, diags);
    if (p.model) {
      p.label = p.model->project.name;
    } else {
      p.label = dep.name;
      p.missing = firstError(diags);
    }
    projects_.push_back(std::move(p));
    const std::size_t idx = projects_.size() - 1;
    byRoot_[loc.root] = idx;
    return idx;
  }

  auto resolvePkgConfig(const DependencyInfo &dep) -> std::size_t {
    Project p;
    p.label = dep.name;

    if (!findOnPath("pkg-config")) {
      p.missing = "pkg-config is not installed";
      projects_.push_back(std::move(p));
      return projects_.size() - 1;
    }

    for (const auto &name : dep.pkgConfig) {
      const auto version =
          runCapture({"pkg-config", "--modversion", name}, true);
      if (!version)
        continue;

      Model m;
      m.project.name = "pkg-config:" + dep.name;
      TargetInfo t;
      t.name = dep.name;
      t.type = TargetType::System;
      t.exported = true;
      m.targets.push_back(std::move(t));

      p.model = std::move(m);
      p.label = "pkg-config " + name + " (" + trimmed(*version) + ")";
      projects_.push_back(std::move(p));
      return projects_.size() - 1;
    }

    std::string tried;
    for (const auto &name : dep.pkgConfig) {
      if (!tried.empty())
        tried += ", ";
      tried += name;
    }
    p.missing = "no pkg-config package found (tried " + tried + ")";
    projects_.push_back(std::move(p));
    return projects_.size() - 1;
  }

  void printRoot() {
    const Project &root = projects_[0];
    std::cout << "project " << root.label << "\n";
    for (const auto &t : root.model->targets) {
      std::string line = t.name + "  " + kindOf(t);
      if (t.exported)
        line += ", exported";
      const bool first = shown_.insert({0, t.name}).second;
      std::cout << line << (first ? "" : "  (see above)") << "\n";
      if (first)
        printChildren(0, t, 1);
    }
  }

  void printChildren(std::size_t owner, const TargetInfo &t, int depth) {
    for (const auto &ref : t.depends)
      printRef(owner, ref.name, depth);
  }

  void printRef(std::size_t owner, const std::string &ref, int depth) {
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    const Project &op = projects_[owner];
    const auto dot = ref.find('.');

    auto projectFor =
        [&](const std::string &alias) -> std::optional<std::size_t> {
      const auto it = op.deps.find(alias);
      if (it == op.deps.end())
        return std::nullopt;
      return it->second;
    };

    std::size_t proj = owner;
    std::string target;

    if (dot == std::string::npos) {
      if (findTargetIn(*op.model, ref)) {
        target = ref;
      } else {
        const DependencyInfo *d = findDependencyIn(*op.model, ref);
        const auto p = projectFor(ref);
        if (d == nullptr || !p) {
          std::cout << indent << ref << "  (no target selected)\n";
          return;
        }
        proj = *p;
        if (d->target) {
          target = *d->target;
        } else if (!d->pkgConfig.empty()) {
          target = ref;
        } else {
          std::cout << indent << ref << "  (no target selected)\n";
          return;
        }
      }
    } else {
      const std::string alias = ref.substr(0, dot);
      target = ref.substr(dot + 1);
      const auto p = projectFor(alias);
      if (!p) {
        std::cout << indent << ref << "  (unresolved)\n";
        return;
      }
      proj = *p;
    }

    printNode(proj, target, ref, depth);
  }

  void printNode(std::size_t proj, const std::string &target,
                 const std::string &label, int depth) {
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    const Project &p = projects_[proj];

    if (!p.model) {
      std::cout << indent << label << "  (" << p.missing << ")\n";
      return;
    }

    const TargetInfo *t = findTargetIn(*p.model, target);
    if (t == nullptr) {
      std::cout << indent << label << "  (no target '" << target
                << "' in project '" << p.label << "')\n";
      return;
    }

    std::string line = indent + label + "  ";
    if (t->type == TargetType::System)
      line += p.label;
    else
      line += kindOf(*t) + (t->exported ? ", exported" : "");

    if (!shown_.insert({proj, target}).second) {
      std::cout << line << "  (see above)\n";
      return;
    }
    std::cout << line << "\n";
    printChildren(proj, *t, depth + 1);
  }

  fs::path rootDir_;
  Lock lock_;
  std::deque<Project> projects_;
  std::map<fs::path, std::size_t> byRoot_;
  std::set<std::pair<std::size_t, std::string>> shown_;
};
} // namespace

auto printTree(const fs::path &root) -> int {
  Tree tree(root);
  return tree.run();
}

} // namespace smelt

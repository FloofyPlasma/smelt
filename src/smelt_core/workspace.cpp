#include "workspace.hpp"
#include "ast.hpp"
#include "git.hpp"
#include "lexer.hpp"
#include "lock.hpp"
#include "parser.hpp"
#include "process.hpp"
#include "registry.hpp"
#include "sema.hpp"
#include "tools.hpp"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace smelt {
auto findTarget(const Package &p, const std::string &name)
    -> const TargetInfo * {
  for (const auto &t : p.model.targets) {
    if (t.name == name)
      return &t;
  }
  return nullptr;
}

auto loadModel(const fs::path &file, Diagnostics &diags)
    -> std::optional<Model> {
  diags.setFile(file.string());

  std::ifstream in(file, std::ios::binary);
  if (!in) {
    diags.error({}, "cannot read " + file.string());
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string source = buffer.str();

  Lexer lexer(source, diags);
  const auto tokens = lexer.tokenize();
  const ProjectFile ast = parseProject(tokens, diags);
  if (diags.hasErrors())
    return std::nullopt;
  return analyze(ast, diags);
}

namespace {
auto findDependency(const Model &m, const std::string &name)
    -> const DependencyInfo * {
  for (const auto &d : m.dependencies) {
    if (d.name == name)
      return &d;
  }
  return nullptr;
}

auto splitWords(const std::string &s) -> std::vector<std::string> {
  std::vector<std::string> out;
  std::istringstream in(s);
  std::string word;
  while (in >> word)
    out.push_back(word);
  return out;
}

auto trimmed(std::string s) -> std::string {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
    s.pop_back();
  return s;
}

class StackGuard {
public:
  StackGuard(std::vector<fs::path> &stack, fs::path p) : stack_(stack) {
    stack_.push_back(std::move(p));
  }
  ~StackGuard() { stack_.pop_back(); }
  StackGuard(const StackGuard &) = delete;
  StackGuard &operator=(const StackGuard &) = delete;

private:
  std::vector<fs::path> &stack_;
};

class Loader {
public:
  explicit Loader(Diagnostics &diags) : diags_(diags) {}

  std::deque<Package> packages;

  auto loadRoot(const fs::path &root) -> std::optional<std::size_t> {
    rootDir_ = fs::canonical(root);
    std::string err;
    if (!readLock(rootDir_ / "smelt.lock", lock_, err)) {
      diags_.error({}, err);
      return std::nullopt;
    }
    return loadProject(rootDir_ / "project.smelt", rootDir_);
  }

private:
  auto loadProject(const fs::path &file, const fs::path &root)
      -> std::optional<std::size_t> {
    StackGuard guard(stack_, root);
    const std::string display = file.string();

    auto model = loadModel(file, diags_);
    if (!model)
      return std::nullopt;

    for (const auto &p : packages) {
      if (p.model.project.name == model->project.name) {
        diags_.error(model->project.loc,
                     "project name '" + model->project.name +
                         "' is used by more than one project");
        return std::nullopt;
      }
    }

    Package pkg;
    pkg.projectFile = display;
    pkg.root = root;
    pkg.model = std::move(*model);
    packages.push_back(std::move(pkg));
    const std::size_t index = packages.size() - 1;
    byDir_[root] = index;

    const std::vector<DependencyInfo> decls =
        packages[index].model.dependencies;
    for (const auto &dep : decls) {
      diags_.setFile(display);
      const auto child = dep.pkgConfig.empty() ? loadDependency(dep, root)
                                               : loadPkgConfig(dep);
      if (!child)
        return std::nullopt;
      packages[index].deps[dep.name] = *child;

      diags_.setFile(display);
      if (!checkExports(dep, *child))
        return std::nullopt;
    }

    diags_.setFile(display);
    if (!resolveDepends(index))
      return std::nullopt;
    return index;
  }

  auto loadDependency(const DependencyInfo &dep, const fs::path &parentRoot)
      -> std::optional<std::size_t> {
    fs::path file;
    fs::path root;
    std::error_code ec;

    if (dep.registry) {
      if (!lockedRegistry(dep, file, root))
        return std::nullopt;
    } else if (dep.git) {
      if (!lockedCheckout(dep, root))
        return std::nullopt;
      file = root / "project.smelt";
    } else {
      const fs::path dir = parentRoot / dep.path;
      if (!fs::exists(dir / "project.smelt", ec)) {
        diags_.error(dep.loc, "dependency '" + dep.name +
                                  "': no project.smelt in '" + dir.string() +
                                  "'");
        return std::nullopt;
      }
      root = fs::canonical(dir);
      file = root / "project.smelt";
    }

    if (std::find(stack_.begin(), stack_.end(), root) != stack_.end()) {
      diags_.error(dep.loc, "dependency cycle: " + cyclePath(root));
      return std::nullopt;
    }
    if (const auto it = byDir_.find(root); it != byDir_.end())
      return it->second;
    return loadProject(file, root);
  }

  auto loadPkgConfig(const DependencyInfo &dep) -> std::optional<std::size_t> {
    if (!smelt::findOnPath("pkg-config")) {
      diags_.error(dep.loc, "required tool 'pkg-config' not found on PATH");
      return std::nullopt;
    }

    for (const auto &name : dep.pkgConfig) {
      const auto version =
          runCapture({"pkg-config", "--modversion", name}, true);
      if (!version)
        continue;

      const auto cflags = runCapture({"pkg-config", "--cflags", name}, true);
      const auto libs = runCapture({"pkg-config", "--libs", name}, true);
      if (!cflags || !libs) {
        diags_.error(dep.loc,
                     "pkg-config could not report flags for '" + name + "'");
        return std::nullopt;
      }

      const std::string ver = trimmed(*version);
      std::cout << dep.name << ": pkg-config " << name << " (" << ver << ")\n";
      return addSystemPackage(dep, ver, *cflags, *libs);
    }

    std::string tried;
    for (const auto &name : dep.pkgConfig) {
      if (!tried.empty())
        tried += ", ";
      tried += name;
    }
    diags_.error(dep.loc, "dependency '" + dep.name +
                              "': no pkg-config package found (tried " + tried +
                              ")");
    return std::nullopt;
  }

  auto addSystemPackage(const DependencyInfo &dep, const std::string &version,
                        const std::string &cflags, const std::string &libs)
      -> std::size_t {
    TargetInfo t;
    t.loc = dep.loc;
    t.name = dep.name;
    t.type = TargetType::System;
    t.exported = true;

    const auto cf = splitWords(cflags);
    for (std::size_t i = 0; i < cf.size(); ++i) {
      const std::string &w = cf[i];
      if (w.rfind("-I", 0) == 0 && w.size() > 2) {
        t.systemIncludes.push_back(w.substr(2));
      } else if (w == "-isystem" && i + 1 < cf.size()) {
        t.systemIncludes.push_back(cf[++i]);
      } else if (w.rfind("-D", 0) == 0 && w.size() > 2) {
        t.defines.push_back(w.substr(2));
      } else {
        t.compileOptions.push_back(w);
      }
    }
    t.link = splitWords(libs);

    ProjectInfo info;
    info.loc = dep.loc;
    info.name = "pkg-config:" + dep.name;
    info.version = version;

    Package pkg;
    pkg.model.project = info;
    pkg.model.targets.push_back(std::move(t));
    packages.push_back(std::move(pkg));
    return packages.size() - 1;
  }

  auto lockedCheckout(const DependencyInfo &dep, fs::path &root) -> bool {
    const auto it = lock_.entries.find(dep.name);
    if (it == lock_.entries.end()) {
      diags_.error(dep.loc, "dependency '" + dep.name +
                                "' is not in smelt.lock; run 'smelt fetch'");
      return false;
    }

    const LockEntry &e = it->second;
    const std::string kind = dep.tag ? "tag" : "commit";
    const std::string ref = dep.tag ? *dep.tag : dep.commit.value_or("");
    if (e.kind != "git" || e.url != *dep.git || e.refKind != kind ||
        e.ref != ref) {
      diags_.error(dep.loc,
                   "registry entry for '" + dep.name +
                       "' differs from smelt.lock; run 'smelt update " +
                       dep.name + "' to move it");
      return false;
    }
    return checkedAt(dep, checkoutDir(rootDir_, dep.name), e.commit, root);
  }

  auto lockedRegistry(const DependencyInfo &dep, fs::path &file, fs::path &root)
      -> bool {
    const auto it = lock_.entries.find(dep.name);
    if (it == lock_.entries.end()) {
      diags_.error(dep.loc, "dependency '" + dep.name +
                                "' is not in smelt.lock; run 'smelt fetch'");
      return false;
    }

    const LockEntry &e = it->second;
    if (e.kind != "registry" || e.ref != *dep.version) {
      diags_.error(dep.loc, "smelt.lock does not match project.smelt for '" +
                                dep.name + "'; run 'smelt fetch'");
      return false;
    }

    const fs::path reg = registryDir(rootDir_);
    std::error_code ec;
    if (!fs::exists(reg, ec) || headCommit(reg) != lock_.registryCommit) {
      diags_.error(dep.loc,
                   "registry entry for '" + dep.name +
                       "' differs from smelt.lock; run 'smelt update " +
                       dep.name + "' to move it");
      return false;
    }

    const auto entry = readRegistryEntry(reg, dep.name, *dep.version, diags_);
    if (!entry)
      return false;
    if (entry->upstreamUrl != e.url || entry->upstreamCommit != e.commit) {
      diags_.error(dep.loc,
                   "registry entry for '" + dep.name +
                       "' differs from smelt.lock; run 'smelt update " +
                       dep.name + "' to move it");
      return false;
    }

    if (!checkedAt(dep, checkoutDir(rootDir_, dep.name), e.commit, root))
      return false;
    file = entry->overlayFile;
    return true;
  }

  auto checkedAt(const DependencyInfo &dep, const fs::path &checkout,
                 const std::string &commit, fs::path &out) -> bool {
    std::error_code ec;
    if (!fs::exists(checkout, ec)) {
      diags_.error(dep.loc, "checkout of '" + dep.name +
                                "' is missing; run 'smelt fetch'");
      return false;
    }
    const auto head = headCommit(checkout);
    if (!head || *head != commit) {
      diags_.error(dep.loc, "checkout of '" + dep.name +
                                "' is not at the locked commit; run "
                                "'smelt fetch'");
      return false;
    }
    out = fs::canonical(checkout);
    return true;
  }

  auto checkExports(const DependencyInfo &dep, std::size_t child) -> bool {
    const Package &c = packages[child];
    const bool any =
        std::any_of(c.model.targets.begin(), c.model.targets.end(),
                    [](const TargetInfo &t) { return t.exported; });
    if (!any) {
      diags_.error(dep.loc, "dependency '" + dep.name + "' exports no targets");
      return false;
    }
    if (dep.target)
      return exportedNode(child, *dep.target, dep.loc).has_value();
    return true;
  }

  auto exportedNode(std::size_t owner, const std::string &target, SourceLoc loc)
      -> std::optional<NodeKey> {
    const Package &p = packages[owner];
    const TargetInfo *t = findTarget(p, target);
    if (!t) {
      diags_.error(loc, "no target '" + target + "' in project '" +
                            p.model.project.name + "'");
      return std::nullopt;
    }
    if (!t->exported) {
      diags_.error(loc, "target '" + target + "' is not exported by project '" +
                            p.model.project.name + "'");
      return std::nullopt;
    }
    return NodeKey{owner, target};
  }

  auto resolveDepends(std::size_t index) -> bool {
    bool ok = true;
    for (std::size_t ti = 0; ti < packages[index].model.targets.size(); ++ti) {
      for (const auto &dep : packages[index].model.targets[ti].depends) {
        if (packages[index].resolved.count(dep.name))
          continue;
        if (auto node = resolveName(index, dep))
          packages[index].resolved[dep.name] = *node;
        else
          ok = false;
      }
    }
    return ok;
  }

  auto resolveName(std::size_t index, const NameRef &ref)
      -> std::optional<NodeKey> {
    const Package &pkg = packages[index];
    const std::size_t dot = ref.name.find('.');

    if (dot == std::string::npos) {
      if (findTarget(pkg, ref.name))
        return NodeKey{index, ref.name};

      const DependencyInfo *dep = findDependency(pkg.model, ref.name);
      if (!dep)
        return std::nullopt;
      if (!dep->target) {
        if (!dep->pkgConfig.empty())
          return NodeKey{pkg.deps.at(ref.name), ref.name};
        diags_.error(ref.loc, "dependency '" + ref.name +
                                  "' selects no target; write '" + ref.name +
                                  ".<target>'");
        return std::nullopt;
      }
      return exportedNode(pkg.deps.at(ref.name), *dep->target, ref.loc);
    }

    const std::string alias = ref.name.substr(0, dot);
    const std::string targetName = ref.name.substr(dot + 1);
    const DependencyInfo *dep = findDependency(pkg.model, alias);
    if (!dep)
      return std::nullopt;
    if (dep->target) {
      diags_.error(ref.loc, "dependency '" + alias + "' selects target '" +
                                *dep->target + "'; write '" + alias +
                                "' instead");
      return std::nullopt;
    }
    return exportedNode(pkg.deps.at(alias), targetName, ref.loc);
  }

  auto cyclePath(const fs::path &again) const -> std::string {
    std::string out;
    const auto start = std::find(stack_.begin(), stack_.end(), again);
    for (auto p = start; p != stack_.end(); ++p)
      out += nameOf(*p) + " -> ";
    return out + nameOf(again);
  }

  auto nameOf(const fs::path &root) const -> std::string {
    for (const auto &p : packages) {
      if (p.root == root)
        return p.model.project.name;
    }
    return root.string();
  }

  fs::path rootDir_;
  Lock lock_;
  std::vector<fs::path> stack_;
  std::map<fs::path, std::size_t> byDir_;
  Diagnostics &diags_;
};
} // namespace

auto Workspace::load(const std::filesystem::path &root, Diagnostics &diags)
    -> std::optional<Workspace> {
  Loader loader(diags);
  if (!loader.loadRoot(root))
    return std::nullopt;

  Workspace ws;
  ws.packages = std::move(loader.packages);
  return ws;
}

} // namespace smelt

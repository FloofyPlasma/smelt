#include "workspace.hpp"
#include "ast.hpp"
#include "diagnostics.hpp"
#include "git.hpp"
#include "lexer.hpp"
#include "lock.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include <algorithm>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

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

auto loadModel(const fs::path &dir, Diagnostics &diags)
    -> std::optional<Model> {
  const fs::path file = dir / "project.smelt";
  diags.setFile(file.string());

  std::ifstream in(file, std::ios::binary);
  if (!in) {
    diags.error({}, "cannot read project file");
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
    return loadProject(root, rootDir_);
  }

private:
  auto loadProject(const fs::path &dir, const fs::path &canon)
      -> std::optional<std::size_t> {
    StackGuard guard(stack_, canon);

    const fs::path file = dir / "project.smelt";
    const std::string display = file.string();
    diags_.setFile(display);

    std::ifstream in(file, std::ios::binary);
    if (!in) {
      diags_.error({}, "cannot read project file");
      return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string source = buffer.str();

    Lexer lexer(source, diags_);
    const auto tokens = lexer.tokenize();
    const ProjectFile ast = parseProject(tokens, diags_);
    if (diags_.hasErrors())
      return std::nullopt;

    auto model = analyze(ast, diags_);
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
    pkg.root = canon;
    pkg.model = std::move(*model);
    packages.push_back(std::move(pkg));
    const std::size_t index = packages.size() - 1;
    byDir_[canon] = index;

    const std::vector<DependencyInfo> decls =
        packages[index].model.dependencies;
    for (const auto &dep : decls) {
      diags_.setFile(display);
      const auto child = loadDependency(dep, canon);
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
    fs::path dir;
    if (dep.git) {
      if (!lockedCheckout(dep, dir))
        return std::nullopt;
    } else {
      dir = parentRoot / dep.path;
    }

    std::error_code ec;
    if (!fs::exists(dir / "project.smelt", ec)) {
      diags_.error(dep.loc, "dependency '" + dep.name +
                                "': no project.smelt in '" + dir.string() +
                                "'");
      return std::nullopt;
    }
    const fs::path canon = fs::canonical(dir);

    if (std::find(stack_.begin(), stack_.end(), canon) != stack_.end()) {
      diags_.error(dep.loc, "dependency cycle: " + cyclePath(canon));
      return std::nullopt;
    }
    if (const auto it = byDir_.find(canon); it != byDir_.end())
      return it->second;
    return loadProject(dir, canon);
  }

  auto lockedCheckout(const DependencyInfo &dep, fs::path &dir) -> bool {
    const auto it = lock_.find(dep.name);
    if (it == lock_.end()) {
      diags_.error(dep.loc, "dependency '" + dep.name +
                                "' is not in smelt.lock; run 'smelt fetch'");
      return false;
    }

    const LockEntry &e = it->second;
    const std::string kind = dep.tag ? "tag" : "commit";
    const std::string ref = dep.tag ? *dep.tag : dep.commit.value_or("");
    if (e.url != *dep.git || e.refKind != kind || e.ref != ref) {
      diags_.error(dep.loc, "smelt.lock does not match project.smelt for '" +
                                dep.name + "'; run 'smelt fetch'");
      return false;
    }

    dir = checkoutDir(rootDir_, dep.name);
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
      diags_.error(dep.loc, "checkout of '" + dep.name +
                                "' is missing; run 'smelt fetch'");
      return false;
    }

    const auto head = headCommit(dir);
    if (!head || *head != e.commit) {
      diags_.error(dep.loc,
                   "checkout of '" + dep.name +
                       "' is not at the locked commit; run 'smelt fetch'");
      return false;
    }
    return true;
  }

  [[nodiscard]] auto cyclePath(const fs::path &again) const -> std::string {
    std::string out;
    const auto start = std::find(stack_.begin(), stack_.end(), again);
    for (auto p = start; p != stack_.end(); ++p)
      out += nameOf(*p) + " -> ";
    return out + nameOf(again);
  }

  [[nodiscard]] auto nameOf(const fs::path &root) const -> std::string {
    for (const auto &p : packages) {
      if (p.root == root)
        return p.model.project.name;
    }
    return root.string();
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
                                *dep->target + "'; wrote '" + alias +
                                "' instead");
      return std::nullopt;
    }
    return exportedNode(pkg.deps.at(alias), targetName, ref.loc);
  }

  std::vector<fs::path> stack_;
  fs::path rootDir_;
  std::map<std::string, LockEntry> lock_;
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

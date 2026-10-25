#include "sema.hpp"
#include "ast.hpp"
#include "diagnostics.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
namespace smelt {
namespace {
auto oneOf(std::string_view s, std::initializer_list<std::string_view> options)
    -> bool {
  return std::find(options.begin(), options.end(), s) != options.end();
}

auto oneOfInt(std::int64_t v, std::initializer_list<int> options) -> bool {
  return std::find(options.begin(), options.end(), v) != options.end();
}

auto escapesProject(const std::string &path) -> bool {
  return (!path.empty() && path.front() == '/') ||
         path.find("..") != std::string::npos;
}

class Checker {
public:
  explicit Checker(Diagnostics &diags) : diags_(diags) {}

  auto run(const ProjectFile &file) -> std::optional<Model> {
    Model m;

    if (file.projects.size() > 1)
      error(file.projects[1].loc, "only one 'project' declaration is allowed");

    m.project = checkProject(file.projects.front());
    m.profiles = checkProfiles(file.profiles);
    m.dependencies = checkDependencyDecls(file.dependencies);
    m.targets = checkTargets(file.targets);
    checkDependencies(m.targets, m.dependencies);

    if (diags_.hasErrors())
      return std::nullopt;
    return m;
  }

private:
  Diagnostics &diags_;

  void error(SourceLoc loc, std::string message) {
    diags_.error(loc, std::move(message));
  }

  auto asString(const Assign &a) -> std::optional<std::string> {
    if (a.value.kind == Value::Kind::String)
      return a.value.text;
    error(a.value.loc, "'" + a.key + "' must be a quoted string");
    return std::nullopt;
  }

  auto asBool(const Assign &a) -> std::optional<bool> {
    if (a.value.kind == Value::Kind::Bool)
      return a.value.boolean;
    error(a.value.loc, "'" + a.key + "' must be true or false");
    return std::nullopt;
  }

  auto asWord(const Assign &a) -> std::optional<std::string> {
    if (a.value.kind == Value::Kind::Word)
      return a.value.text;
    error(a.value.loc, "'" + a.key + "' must be a bare word");
    return std::nullopt;
  }

  auto asInt(const Assign &a) -> std::optional<std::int64_t> {
    if (a.value.kind == Value::Kind::Integer)
      return a.value.integer;
    error(a.value.loc, "'" + a.key + "' must be an integer");
    return std::nullopt;
  }

  auto asStringList(const Assign &a)
      -> std::optional<std::vector<std::string>> {
    if (a.value.kind != Value::Kind::List) {
      error(a.value.loc, "'" + a.key + "' must be a list of quoted strings");
      return std::nullopt;
    }
    std::vector<std::string> out;
    for (const auto &item : a.value.items) {
      if (item.kind != Value::Kind::String) {
        error(item.loc, "items in '" + a.key + "' must be quoted strings");
        return std::nullopt;
      }
      out.push_back(item.text);
    }
    return out;
  }

  void assignList(const Assign &a, std::vector<std::string> &out) {
    if (auto list = asStringList(a))
      out = std::move(*list);
  }

  void checkDuplicates(const std::vector<Assign> &assigns) {
    std::set<std::string> seen;
    for (const auto &a : assigns) {
      if (!seen.insert(a.key).second)
        error(a.loc, "setting '" + a.key + "' is already set");
    }
  }

  void unknownKey(const Assign &a, std::string_view block) {
    if (a.key == "sources") {
      error(a.loc, "'sources' is not a valid setting; targets find their files "
                   "automatically, and 'files' filters them");
    } else {
      error(a.loc, "unknown setting '" + a.key + "' in " + std::string(block));
    }
  }

  auto checkProject(const ProjectDecl &decl) -> ProjectInfo {
    ProjectInfo p;
    p.loc = decl.loc;
    p.name = decl.name;

    if (p.name.empty())
      error(decl.loc, "project name must not be empty");

    checkDuplicates(decl.assigns);

    for (const auto &a : decl.assigns) {
      if (a.key == "version") {
        if (auto v = asString(a))
          p.version = *v;
      } else if (a.key == "cxx_std") {
        if (auto v = asInt(a)) {
          if (oneOfInt(*v, {11, 14, 17, 20, 23, 26}))
            p.cxxStd = static_cast<int>(*v);
          else
            error(a.value.loc, "cxx_std must be 11, 14, 17, 20, 23, or 26");
        }
      } else if (a.key == "c_std") {
        if (auto v = asInt(a)) {
          if (oneOfInt(*v, {99, 11, 17, 23}))
            p.cStd = static_cast<int>(*v);
          else
            error(a.value.loc, "c_std must be 99, 11, 17, or 23");
        }
      } else {
        unknownKey(a, "project");
      }
    }
    return p;
  }

  auto checkProfiles(const std::vector<ProfileDecl> &decls)
      -> std::vector<ProfileInfo> {
    std::vector<ProfileInfo> out{
        {"debug", Optimization::None, true},
        {"release", Optimization::Speed, false},
    };
    std::set<std::string> seen;

    for (const auto &d : decls) {
      if (!seen.insert(d.name).second) {
        error(d.loc, "profile '" + d.name + "' is declared twice");
        continue;
      }

      auto it = std::find_if(out.begin(), out.end(), [&](const ProfileInfo &p) {
        return p.name == d.name;
      });
      if (it == out.end()) {
        error(d.loc, "profile name must be 'debug' or 'release', found '" +
                         d.name + "'");
        continue;
      }

      checkDuplicates(d.assigns);

      for (const auto &a : d.assigns) {
        if (a.key == "optimization") {
          if (auto w = asWord(a)) {
            if (*w == "none")
              it->optimization = Optimization::None;
            else if (*w == "speed")
              it->optimization = Optimization::Speed;
            else if (*w == "size")
              it->optimization = Optimization::Size;
            else
              error(a.value.loc, "optimization must be none, speed, or size");
          }
        } else if (a.key == "debug_info") {
          if (auto b = asBool(a))
            it->debugInfo = *b;
        } else if (a.key == "defines") {
          if (auto list = asStringList(a))
            it->defines = std::move(*list);
        } else {
          unknownKey(a, "profile");
        }
      }
    }
    return out;
  }

  auto checkTargets(const std::vector<TargetDecl> &decls)
      -> std::vector<TargetInfo> {
    std::vector<TargetInfo> out;
    std::set<std::string> seen;

    for (const auto &d : decls) {
      TargetInfo t = checkTarget(d);
      if (!seen.insert(t.name).second) {
        error(d.loc, "target '" + t.name + "' is declared twice");
        continue;
      }
      out.push_back(std::move(t));
    }
    return out;
  }

  auto checkTarget(const TargetDecl &d) -> TargetInfo {
    TargetInfo t;
    t.loc = d.loc;
    t.name = d.name;

    if (t.name.empty())
      error(d.loc, "target name must not be empty");
    else if (t.name.find('.') != std::string::npos)
      error(d.loc, "target name '" + t.name + "' must not contain '.'");

    checkDuplicates(d.assigns);

    bool hasType = false;
    std::optional<SourceLoc> exportLoc;

    for (const auto &a : d.assigns) {
      if (a.key == "type") {
        hasType = true;
        if (auto w = asWord(a)) {
          if (*w == "exe")
            t.type = TargetType::Exe;
          else if (*w == "static_lib")
            t.type = TargetType::StaticLib;
          else
            error(a.value.loc, "type must be exe or static_lib");
        }
      } else if (a.key == "source_dir") {
        if (auto s = asString(a)) {
          if (escapesProject(*s))
            error(a.value.loc,
                  "source_dir must be a relative path inside the project");
          else
            t.sourceDir = *s;
        }
      } else if (a.key == "export") {
        exportLoc = a.loc;
        if (auto b = asBool(a))
          t.exported = *b;
      } else if (a.key == "files") {
        assignList(a, t.files);
      } else if (a.key == "exclude") {
        assignList(a, t.exclude);
      } else if (a.key == "public_includes") {
        assignList(a, t.publicIncludes);
      } else if (a.key == "private_includes") {
        assignList(a, t.privateIncludes);
      } else if (a.key == "defines") {
        assignList(a, t.defines);
      } else if (a.key == "compile_options") {
        assignList(a, t.compileOptions);
      } else if (a.key == "link") {
        assignList(a, t.link);
      } else if (a.key == "depends") {
        checkDepends(a, t);
      } else {
        unknownKey(a, "target");
      }
    }

    if (!hasType)
      error(d.loc, "target '" + t.name + "' has no 'type'");

    if (t.exported && t.type == TargetType::Exe && exportLoc)
      error(*exportLoc, "only static_lib targets can be exported");

    for (const auto &block : d.onBlocks)
      checkPlatformBlock(block, t);

    return t;
  }

  void checkDepends(const Assign &a, TargetInfo &t) {
    if (a.value.kind != Value::Kind::List) {
      error(a.value.loc, "'depends' must be a list of quoted strings");
      return;
    }
    for (const auto &item : a.value.items) {
      if (item.kind != Value::Kind::String) {
        error(item.loc, "items in 'depends' must be quoted strings");
        continue;
      }
      t.depends.push_back({item.loc, item.text});
    }
  }

  auto checkDependencyDecls(const std::vector<DependencyDecl> &decls)
      -> std::vector<DependencyInfo> {
    std::vector<DependencyInfo> out;
    std::set<std::string> seen;

    for (const auto &d : decls) {
      if (d.name.empty() || d.name.find('.') != std::string::npos) {
        error(d.loc, "dependency name '" + d.name + "' is not valid");
        continue;
      }
      if (!seen.insert(d.name).second) {
        error(d.loc, "dependency '" + d.name + "' is declared twice");
        continue;
      }

      DependencyInfo info;
      info.loc = d.loc;
      info.name = d.name;
      checkDuplicates(d.assigns);

      for (const auto &a : d.assigns) {
        if (a.key == "path") {
          if (auto s = asString(a))
            info.path = *s;
        } else if (a.key == "target") {
          if (auto s = asString(a))
            info.target = *s;
        } else if (oneOf(a.key, {"git", "registry", "version", "tag", "commit",
                                 "find"})) {
          error(a.loc, "dependency source '" + a.key +
                           "' is not supported yet; only 'path' is supported");
        } else {
          unknownKey(a, "dependency");
        }
      }

      if (info.path.empty())
        error(d.loc, "dependency '" + d.name + "' needs a 'path'");
      else
        out.push_back(std::move(info));
    }
    return out;
  }

  void checkPlatformBlock(const OnBlock &block, TargetInfo &t) {
    if (!oneOf(block.platform, {"linux", "macos", "windows"})) {
      error(block.loc, "unknown platform '" + block.platform + "'");
      return;
    }
    if (t.platforms.count(block.platform)) {
      error(block.loc,
            "platform '" + block.platform + "' is declared twice in target");
      return;
    }

    checkDuplicates(block.assigns);

    PlatformRules rules;
    for (const auto &a : block.assigns) {
      if (a.key == "files")
        assignList(a, rules.files);
      else if (a.key == "exclude")
        assignList(a, rules.exclude);
      else if (a.key == "link")
        assignList(a, rules.link);
      else
        error(a.loc, "setting '" + a.key + "' is not allowed in an 'on' block");
    }
    t.platforms[block.platform] = std::move(rules);
  }

  void checkDependencies(const std::vector<TargetInfo> &targets,
                         const std::vector<DependencyInfo> &deps) {
    std::map<std::string, const TargetInfo *> byName;
    for (const auto &t : targets)
      byName[t.name] = &t;

    std::set<std::string> depNames;
    for (const auto &d : deps)
      depNames.insert(d.name);

    for (const auto &t : targets) {
      if (depNames.count(t.name))
        error(t.loc, "'" + t.name + "' is both a target and a dependency");

      for (const auto &dep : t.depends) {
        const std::size_t dot = dep.name.find('.');
        if (dot == std::string::npos) {
          if (!byName.count(dep.name) && !depNames.count(dep.name))
            error(dep.loc, "unknown target '" + dep.name + "' in depends");
          continue;
        }
        const std::string alias = dep.name.substr(0, dot);
        const std::string rest = dep.name.substr(dot + 1);
        if (!depNames.count(alias))
          error(dep.loc, "unknown dependency '" + alias + "' in depends");
        else if (rest.empty() || rest.find('.') != std::string::npos)
          error(dep.loc, "'" + dep.name + "' is not a valid target reference");
      }
    }

    enum class Mark : std::uint8_t { None, Active, Done };
    std::map<std::string, Mark> marks;
    std::vector<std::string> stack;

    std::function<void(const TargetInfo &)> visit = [&](const TargetInfo &t) {
      marks[t.name] = Mark::Active;
      stack.push_back(t.name);

      for (const auto &dep : t.depends) {
        const auto it = byName.find(dep.name);
        if (it == byName.end())
          continue;

        const Mark m = marks[dep.name];
        if (m == Mark::Active) {
          std::string path;
          const auto start = std::find(stack.begin(), stack.end(), dep.name);
          for (auto p = start; p != stack.end(); ++p)
            path += *p + " -> ";
          path += dep.name;
          error(dep.loc, "dependency cycle: " + path);
        } else if (m == Mark::None) {
          visit(*it->second);
        }
      }

      stack.pop_back();
      marks[t.name] = Mark::Done;
    };

    for (const auto &t : targets) {
      if (marks[t.name] == Mark::None)
        visit(t);
    }
  }
};
} // namespace

auto analyze(const ProjectFile &file, Diagnostics &diags)
    -> std::optional<Model> {
  if (file.projects.empty())
    return std::nullopt;

  Checker checker(diags);
  return checker.run(file);
}

} // namespace smelt

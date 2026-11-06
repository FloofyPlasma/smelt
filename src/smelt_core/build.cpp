#include "build.hpp"
#include "console.hpp"
#include "discover.hpp"
#include "hash.hpp"
#include "module_scan.hpp"
#include "process.hpp"
#include "sema.hpp"
#include "std_module.hpp"
#include "workspace.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace smelt {
namespace {
auto readText(const fs::path &p) -> std::optional<std::string> {
  std::ifstream in(p, std::ios::binary);
  if (!in)
    return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
auto readDepFile(const fs::path &depFile)
    -> std::optional<std::vector<std::string>> {
  const auto raw = readText(depFile);
  if (!raw)
    return std::nullopt;

  std::string s;
  for (std::size_t i = 0; i < raw->size(); ++i) {
    const char c = (*raw)[i];
    if (c == '\\' && i + 1 < raw->size() && (*raw)[i + 1] == '\n') {
      ++i;
      continue;
    }
    if (c == '\\' && i + 2 < raw->size() && (*raw)[i + 1] == '\r' &&
        (*raw)[i + 2] == '\n') {
      i += 2;
      continue;
    }
    s.push_back(c);
  }

  const std::size_t colon = s.find(':');
  if (colon == std::string::npos)
    return std::nullopt;

  std::vector<std::string> deps;
  std::string cur;
  bool inToken = false;
  auto flush = [&] {
    if (inToken) {
      deps.push_back(cur);
      cur.clear();
      inToken = false;
    }
  };

  for (std::size_t i = colon + 1; i < s.size(); ++i) {
    const char c = s[i];
    if (c == '\\' && i + 1 < s.size() && s[i + 1] == ' ') {
      cur.push_back(' ');
      inToken = true;
      ++i;
    } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      flush();
    } else {
      cur.push_back(c);
      inToken = true;
    }
  }
  flush();
  return deps;
}

auto isStale(const fs::path &out, const fs::path &sig,
             const std::string &sigText, const std::vector<fs::path> &inputs)
    -> bool {
  if (!fs::exists(out))
    return true;

  const auto saved = readText(sig);
  if (!saved || *saved != sigText)
    return true;

  const auto outTime = fs::last_write_time(out);
  for (const auto &in : inputs) {
    if (!fs::exists(in))
      return true;
    if (fs::last_write_time(in) > outTime)
      return true;
  }
  return false;
}

void writeSignature(const fs::path &sig, const std::string &sigText) {
  std::ofstream out(sig, std::ios::binary);
  out << sigText;
}

void printLog(const fs::path &log, Console &console) {
  const auto text = readText(log);
  if (!text || text->empty())
    return;
  std::string s = *text;
  if (!s.empty() && s.back() == '\n')
    s.pop_back();
  console.err(s);
}

enum class Outcome { Built, Cached, Failed };

Outcome runCompile(const std::vector<std::string> &args, const fs::path &obj,
                   const fs::path &dep, const fs::path &sig,
                   const std::string &sigText, const fs::path &source,
                   const std::vector<fs::path> &extra, Console &console) {
  const fs::path log = fs::path(obj.string() + ".log");

  const auto deps = readDepFile(dep);
  std::vector<fs::path> inputs{source};
  inputs.insert(inputs.end(), extra.begin(), extra.end());
  if (deps) {
    for (const auto &d : *deps)
      inputs.emplace_back(d);
  }
  if (deps && !isStale(obj, sig, sigText, inputs)) {
    printLog(log, console);
    return Outcome::Cached;
  }

  fs::create_directories(obj.parent_path());
  fs::remove(sig);

  const int rc = runProcessLogged(args, log.string());
  printLog(log, console);

  if (rc != 0) {
    console.err("smelt: compile failed: " + joinCommand(args));
    return Outcome::Failed;
  }
  writeSignature(sig, sigText);
  return Outcome::Built;
}

Outcome runPrecompile(const std::vector<std::string> &args, const fs::path &pcm,
                      const fs::path &sig, const std::string &sigText,
                      const std::vector<fs::path> &inputs, Console &console) {
  const fs::path log(pcm.string() + ".log");
  if (!isStale(pcm, sig, sigText, inputs)) {
    printLog(log, console);
    return Outcome::Cached;
  }

  fs::create_directories(pcm.parent_path());
  fs::remove(pcm);
  fs::remove(sig);

  const int rc = runProcessLogged(args, log.string());
  printLog(log, console);
  if (rc != 0) {
    console.err("smelt: module precompile failed: " + joinCommand(args));
    return Outcome::Failed;
  }
  writeSignature(sig, sigText);
  return Outcome::Built;
}

Outcome runArchive(const std::vector<std::string> &args, const fs::path &out,
                   const fs::path &sig, const std::string &sigText,
                   const std::vector<fs::path> &objects, Console &console) {
  if (!isStale(out, sig, sigText, objects))
    return Outcome::Cached;

  fs::create_directories(out.parent_path());
  fs::remove(out);
  fs::remove(sig);

  if (runProcess(args) != 0) {
    console.err("smelt: archive failed " + joinCommand(args));
    return Outcome::Failed;
  }
  writeSignature(sig, sigText);
  return Outcome::Built;
}

Outcome runLink(const std::vector<std::string> &args, const fs::path &out,
                const fs::path &sig, const std::string &sigText,
                const std::vector<fs::path> &inputs, Console &console) {
  if (!isStale(out, sig, sigText, inputs))
    return Outcome::Cached;

  fs::create_directories(out.parent_path());
  fs::remove(out);
  fs::remove(sig);

  if (runProcess(args) != 0) {
    console.err("smelt: link failed: " + joinCommand(args));
    return Outcome::Failed;
  }
  writeSignature(sig, sigText);
  return Outcome::Built;
}

Outcome runStdModule(const std::vector<std::string> &args, const fs::path &pcm,
                     const fs::path &log, const fs::path &sig,
                     const std::string &sigText, const fs::path &source,
                     Console &console) {
  if (!isStale(pcm, sig, sigText, {source}))
    return Outcome::Cached;

  fs::create_directories(pcm.parent_path());
  fs::remove(pcm);
  fs::remove(sig);

  const int rc = runProcessLogged(args, log.string());
  printLog(log, console);
  if (rc != 0) {
    console.err("smelt: std module precompile failed: " + joinCommand(args));
    return Outcome::Failed;
  }
  writeSignature(sig, sigText);
  return Outcome::Built;
}

void dedupe(std::vector<std::string> &v) {
  std::set<std::string> seen;
  std::vector<std::string> out;
  for (auto &s : v) {
    if (seen.insert(s).second)
      out.push_back(s);
  }
  v = std::move(out);
}

auto stdFlag(const ProjectInfo &pr, const bool cxx) -> std::string {
  if (cxx) {
    const int s = pr.cxxStd.value_or(20);
    return "-std=c++" + std::to_string(s);
  }
  const int s = pr.cStd.value_or(17);
  return "-std=c" + std::to_string(s);
}

auto parseDefine(const std::string &text, const std::string &source,
                 const int rank) -> BuildDefine {
  BuildDefine d;
  d.source = source;
  d.rank = rank;
  const std::size_t eq = text.find('=');
  if (eq == std::string::npos) {
    d.name = text;
  } else {
    d.name = text.substr(0, eq);
    d.value = text.substr(eq + 1);
    d.hasValue = true;
  }
  return d;
}

struct Job {
  std::function<Outcome()> run;
  std::vector<std::size_t> next;
  std::size_t waiting = 0;
  std::string label;
  std::string target;
  bool endsTarget = false;
};
} // namespace

Builder::Builder(const Workspace &ws, const std::vector<SourceMap> &sources,
                 ProfileInfo profile)
    : ws_(ws), sources_(sources), profile_(std::move(profile)) {
  stdEnabled_ = ws_.packages[0].model.project.stdModule;
}

auto Builder::target(const NodeKey &n) const -> const TargetInfo & {
  return *findTarget(ws_.packages[n.first], n.second);
}

auto Builder::prepareStd() -> bool {
  if (!stdEnabled_ || stdSource_)
    return true;

  const auto major = clangMajorVersion();
  if (!major) {
    std::cerr << "smelt: std_module needs clang++ on PATH\n";
    return false;
  }
  if (*major < kMinClangForStdModule) {
    std::cerr << "smelt: std_module needs clang " << kMinClangForStdModule
              << " or newer; found clang " << *major << "\n";
    return false;
  }

  std::string err;
  const auto src = findStdModule(err);
  if (!src) {
    std::cerr << "smelt: " << err << "\n";
    return false;
  }
  stdSource_ = *src;
  return true;
}

auto Builder::stdPcm() const -> fs::path {
  return ws_.packages[0].root / "build" / profile_.name / "std" / "std.pcm";
}

auto Builder::depsOf(const NodeKey &n) const -> std::vector<NodeKey> {
  const Package &p = ws_.packages[n.first];
  std::vector<NodeKey> out;
  for (const auto &dep : target(n).depends)
    out.push_back(p.resolved.at(dep.name));
  return out;
}

auto Builder::outDir(const NodeKey &n) const -> fs::path {
  const fs::path base = ws_.packages[0].root / "build" / profile_.name;
  if (n.first == 0)
    return base / n.second;
  return base / "deps" / ws_.packages[n.first].model.project.name / n.second;
}

void Builder::addPublic(const NodeKey &n, Usage &u) const {
  const Package &p = ws_.packages[n.first];
  const TargetInfo &t = target(n);
  const std::string owner = p.model.project.name + ":" + t.name;

  for (const auto &d : t.publicIncludes)
    u.includes.push_back((p.root / d).lexically_normal().string());

  for (const auto &d : t.systemIncludes)
    u.systemIncludes.push_back(d);

  for (const auto &raw : t.defines) {
    BuildDefine def = parseDefine(raw, owner, 3);
    u.defines.push_back(std::move(def));
  }

  for (const auto &raw : t.publicDefines) {
    u.defines.push_back(parseDefine(raw, owner, 3));
  }

  u.options.insert(u.options.end(), t.compileOptions.begin(),
                   t.compileOptions.end());
  u.link.insert(u.link.end(), t.link.begin(), t.link.end());
}

void Builder::walkDeps(const NodeKey &n, std::set<NodeKey> &seen,
                       Usage &u) const {
  for (const auto &d : depsOf(n)) {
    if (seen.insert(d).second) {
      addPublic(d, u);
      walkDeps(d, seen, u);
    }
  }
}

auto Builder::requirementsOf(const NodeKey &n) const -> Usage {
  const Package &p = ws_.packages[n.first];
  const TargetInfo &t = target(n);

  Usage u;
  u.includes.push_back(
      sources_[n.first].at(n.second).root.lexically_normal().string());
  for (const auto &d : t.privateIncludes)
    u.includes.push_back((p.root / d).lexically_normal().string());

  for (const auto &raw : profile_.defines) {
    u.defines.push_back(parseDefine(raw, "profile " + profile_.name, 1));
  }

  const std::string owner = p.model.project.name + ":" + t.name;
  for (const auto &raw : t.defines)
    u.defines.push_back(parseDefine(raw, owner, 2));

  switch (t.warnings) {
  case WarningLevel::All:
    u.warnings = {"-Wall", "-Wextra"};
    break;
  case WarningLevel::Default:
    break;
  case WarningLevel::None:
    u.warnings = {"-w"};
    break;
  }

  addPublic(n, u);
  std::set<NodeKey> seen{n};
  walkDeps(n, seen, u);

  dedupe(u.includes);
  dedupe(u.systemIncludes);
  dedupe(u.options);
  dedupe(u.link);
  return u;
}

auto Builder::reachable(const std::vector<NodeKey> &roots) const
    -> std::vector<NodeKey> {
  std::vector<NodeKey> out;
  std::set<NodeKey> seen;
  std::function<void(const NodeKey &)> walk = [&](const NodeKey &n) {
    if (!seen.insert(n).second)
      return;
    for (const auto &d : depsOf(n))
      walk(d);
    if (target(n).type != TargetType::System)
      out.push_back(n);
  };
  for (const auto &r : roots)
    walk(r);
  return out;
}

auto Builder::displayName(const NodeKey &n) const -> std::string {
  const Package &p = ws_.packages[n.first];
  return n.first == 0 ? n.second : p.model.project.name + "/" + n.second;
}

auto Builder::closureOf(const NodeKey &node) const -> std::set<NodeKey> {
  std::set<NodeKey> out;
  std::function<void(const NodeKey &)> walk = [&](const NodeKey &n) {
    for (const auto &d : depsOf(n)) {
      if (out.insert(d).second)
        walk(d);
    }
  };
  walk(node);
  return out;
}

auto Builder::resolveImports(const NodeKey &node, const ScanRule &rule,
                             std::vector<std::string> &flags,
                             std::vector<fs::path> &inputs,
                             std::string &err) const -> bool {
  const std::set<NodeKey> closure = closureOf(node);
  bool crossTarget = false;

  for (const auto &n : rule.needs) {
    if (n.sourcePath.empty()) {
      if (n.name == "std" || n.name.rfind("std.", 0) == 0)
        continue;
      err = "imports '" + n.name + "', which is not a known module";
      return false;
    }

    const auto it = providers_.find(n.name);
    if (it == providers_.end()) {
      err = "imports module '" + n.name +
            "', which no unit in the workspace provides";
      return false;
    }

    const NodeKey owner = it->second.owner;
    if (owner == node) {
      const fs::path pcm = outDir(node) / "modules" / (n.name + ".pcm");
      flags.push_back("-fmodule-file=" + n.name + "=" + pcm.string());
      inputs.push_back(pcm);
      continue;
    }

    crossTarget = true;
    if (!closure.count(owner)) {
      err = "imports module '" + n.name + "' from '" + displayName(owner) +
            "', which this target does not depend on";
      return false;
    }
    if (!target(owner).exported) {
      err = "imports module '" + n.name + "' from '" + displayName(owner) +
            "', which does not export it";
      return false;
    }
    inputs.push_back(outDir(owner) / "modules" / (n.name + ".pcm"));
  }

  if (crossTarget || !rule.provides.empty() || !rule.needs.empty()) {
    std::set<NodeKey> providing;
    for (const auto &[name, p] : providers_)
      providing.insert(p.owner);

    std::set<NodeKey> scope = closure;
    scope.insert(node);
    std::set<fs::path> dirs;
    for (const auto &t : scope) {
      if (providing.count(t))
        dirs.insert(outDir(t) / "modules");
    }
    for (const auto &d : dirs)
      flags.push_back("-fprebuilt-module-path=" + d.string());
  }

  return true;
}

auto Builder::scanWorkspace(const std::vector<NodeKey> &roots) -> bool {
  if (scanned_)
    return true;

  std::vector<ScanUnit> units;
  std::map<std::string, NodeKey> ownerOfUnit;

  for (const auto &n : reachable(roots)) {
    const Package &pkg = ws_.packages[n.first];
    const TargetSources &src = sources_[n.first].at(n.second);
    const Usage reqs = requirementsOf(n);
    const fs::path objDir = outDir(n) / "obj";

    for (const auto &file : src.files) {
      if (!file.isCxx || !mayUseModules(file.path))
        continue;
      const fs::path obj = objDir / (file.relPath + ".o");
      const fs::path depFile = fs::path(obj.string() + ".d");
      units.push_back({pkg.root, file.path, obj,
                       compileArgs(pkg, file, reqs, obj, depFile, {})});
      ownerOfUnit[obj.string()] = n;
    }
  }

  if (units.empty()) {
    scanned_ = true;
    return true;
  }

  const fs::path db = ws_.packages[0].root / "build" / profile_.name / "scan" /
                      "compile_commands.json";
  std::map<std::string, ScanRule> byOutput;
  std::string err;
  if (!scanModules(units, db, byOutput, err)) {
    std::cerr << "smelt: " << err << "\n";
    return false;
  }

  rules_ = byOutput;

  for (const auto &u : units) {
    const auto rule = byOutput.find(u.output.string());
    if (rule == byOutput.end())
      continue;
    const NodeKey owner = ownerOfUnit.at(u.output.string());
    for (const auto &p : rule->second.provides) {
      const auto existing = providers_.find(p.name);
      if (existing != providers_.end()) {
        std::cerr
            << "smelt: module '" << p.name << "' is provided by both '"
            << ws_.packages[existing->second.owner.first].model.project.name
            << ":" << existing->second.owner.second << "' and '"
            << ws_.packages[owner.first].model.project.name << ":"
            << owner.second << "\n";
        return false;
      }
      providers_[p.name] = Provider{owner, u.file};
    }
  }

  scanned_ = true;
  return true;
}

void Builder::staticLibs(const NodeKey &n, std::set<NodeKey> &seen,
                         std::vector<NodeKey> &post) const {
  for (const auto &d : depsOf(n)) {
    if (seen.insert(d).second) {
      staticLibs(d, seen, post);
      if (target(d).type == TargetType::StaticLib)
        post.push_back(d);
    }
  }
}

auto Builder::compileArgs(const Package &pkg, const SourceFile &src,
                          const Usage &u, const std::filesystem::path &obj,
                          const std::filesystem::path &dep,
                          const std::vector<std::string> &moduleArgs) const
    -> std::vector<std::string> {
  std::vector<std::string> args{
      src.isCxx ? "clang++" : "clang",
      "-c",
      src.path.string(),
      "-o",
      obj.string(),
      "-MMD",
      "-MF",
      dep.string(),
      stdFlag(stdEnabled_ && src.isCxx ? ws_.packages[0].model.project
                                       : pkg.model.project,
              src.isCxx),
  };

  args.insert(args.end(), u.warnings.begin(), u.warnings.end());

  if (stdEnabled_ && src.isCxx) {
    args.push_back("-stdlib=libc++");
    args.push_back("-fmodule-file=std=" + stdPcm().string());
  }
  args.insert(args.end(), moduleArgs.begin(), moduleArgs.end());

  switch (profile_.optimization) {
  case Optimization::None:
    args.push_back("-O0");
    break;
  case Optimization::Speed:
    args.push_back("-O2");
    break;
  case Optimization::Size:
    args.push_back("-Os");
    break;
  }
  if (profile_.debugInfo)
    args.push_back("-g");

  for (const auto &d : resolveDefines(u).value_or(std::vector<std::string>{}))
    args.push_back("-D" + d);

  args.insert(args.end(), u.options.begin(), u.options.end());
  for (const auto &s : u.systemIncludes) {
    args.push_back("-isystem");
    args.push_back(s);
  }
  for (const auto &i : u.includes)
    args.push_back("-I" + i);

  return args;
}

auto Builder::resolveDefines(const Usage &u) const
    -> std::optional<std::vector<std::string>> {
  std::map<std::string, std::map<std::string, BuildDefine>> bySource;
  std::map<std::string, std::size_t> sourceOrder;

  for (const auto &d : u.defines) {
    bySource[d.source][d.name] = d;
    sourceOrder.emplace(d.source, sourceOrder.size());
  }

  std::vector<BuildDefine> sourcesFlat;
  for (const auto &[source, macros] : bySource) {
    for (const auto &[name, def] : macros)
      sourcesFlat.push_back(def);
  }
  std::stable_sort(sourcesFlat.begin(), sourcesFlat.end(),
                   [](const BuildDefine &a, const BuildDefine &b) {
                     return a.rank < b.rank;
                   });

  std::map<std::string, BuildDefine> merged;
  std::vector<std::string> order;
  bool ok = true;

  for (const auto &d : sourcesFlat) {
    const auto it = merged.find(d.name);
    if (it == merged.end()) {
      merged.emplace(d.name, d);
      order.push_back(d.name);
      continue;
    }

    const std::string a = it->second.hasValue ? it->second.value : "1";
    const std::string b = d.hasValue ? d.value : "1";
    if (a != b) {
      std::cerr << "smelt: macro '" << d.name << "' is defined as '" << a
                << "' by " << it->second.source << " and as '" << b << "' by "
                << d.source << "\n";
      ok = false;
    }
  }

  if (!ok)
    return std::nullopt;

  std::vector<std::string> out;
  for (const auto &name : order) {
    const BuildDefine &d = merged.at(name);
    out.push_back(d.hasValue ? d.name + "=" + d.value : d.name);
  }
  return out;
}

auto Builder::scanNode(const NodeKey &node) const -> std::optional<ModuleInfo> {
  const TargetSources &src = sources_[node.first].at(node.second);
  const fs::path objDir = outDir(node) / "obj";

  ModuleInfo info;
  info.scans.resize(src.files.size());
  for (std::size_t i = 0; i < src.files.size(); ++i) {
    const auto &file = src.files[i];
    if (!file.isCxx)
      continue;
    const fs::path obj = objDir / (file.relPath + ".o");
    if (const auto it = rules_.find(obj.string()); it != rules_.end())
      info.scans[i] = it->second;
  }

  for (std::size_t i = 0; i < src.files.size(); ++i) {
    for (const auto &p : info.scans[i].provides) {
      if (!info.providerOf.emplace(p.name, i).second) {
        std::cerr << "smelt: module '" << p.name
                  << "' is provided by two files in '" << target(node).name
                  << "'\n";
        return std::nullopt;
      }
    }
  }
  return info;
}

[[nodiscard]] auto
Builder::compileUnits(const std::vector<std::string> &rootTargets)
    -> std::optional<std::vector<CompileUnit>> {
  std::vector<CompileUnit> out;
  std::set<NodeKey> visited;

  if (!prepareStd())
    return std::nullopt;

  std::vector<NodeKey> roots;
  for (const auto &name : rootTargets)
    roots.push_back(NodeKey{0, name});
  if (!scanWorkspace(roots))
    return std::nullopt;

  std::function<bool(const NodeKey &)> visit = [&](const NodeKey &node) {
    if (!visited.insert(node).second)
      return true;

    if (target(node).type == TargetType::System)
      return true;

    for (const auto &dep : depsOf(node)) {
      if (target(dep).type == TargetType::Exe) {
        std::cerr << "smelt: target '" << target(node).name
                  << "' depends on exe '" << dep.second << "'\n";
        return false;
      }
      if (!visit(dep))
        return false;
    }

    const Package &pkg = ws_.packages[node.first];
    const TargetSources &src = sources_[node.first].at(node.second);
    const Usage reqs = requirementsOf(node);
    if (!resolveDefines(reqs))
      return false;

    const fs::path modDir = outDir(node) / "modules";
    const fs::path objDir = outDir(node) / "obj";
    const auto info = scanNode(node);
    if (!info)
      return false;

    for (std::size_t i = 0; i < src.files.size(); ++i) {
      const auto &file = src.files[i];
      const fs::path obj = objDir / (file.relPath + ".o");
      const fs::path dep = fs::path(obj.string() + ".d");

      std::vector<std::string> flags;
      std::vector<fs::path> inputs;
      std::string err;
      if (file.isCxx &&
          !resolveImports(node, info->scans[i], flags, inputs, err)) {
        std::cerr << "smelt: " << displayName(node) << ": " << err << "\n";
        return false;
      }

      out.push_back({ws_.packages[0].root, file.path, obj,
                     compileArgs(pkg, file, reqs, obj, dep, flags)});
    }
    return true;
  };

  for (const auto &name : rootTargets) {
    if (!visit({0, name}))
      return std::nullopt;
  }
  return out;
}

auto Builder::buildSet(const std::vector<NodeKey> &roots, std::size_t jobs)
    -> std::optional<std::map<NodeKey, fs::path>> {
  std::deque<Job> graph;
  std::map<NodeKey, std::size_t> producer;
  std::map<NodeKey, fs::path> outputPath;
  std::set<NodeKey> planned;
  bool planOk = true;

  if (!prepareStd())
    return std::nullopt;

  if (!scanWorkspace(roots))
    return std::nullopt;

  std::size_t stdJob = 0;
  std::map<std::string, std::size_t> precompileOf;
  if (stdEnabled_) {
    const fs::path pcm = stdPcm();
    const fs::path sig(pcm.string() + ".sig");
    const fs::path log(pcm.string() + ".log");
    const fs::path src = *stdSource_;
    const std::vector<std::string> args{
        "clang++",
        stdFlag(ws_.packages[0].model.project, true),
        "-stdlib=libc++",
        "--precompile",
        "-Wno-reserved-module-identifier",
        "-o",
        pcm.string(),
        src.string(),
    };
    const std::string sigText = signatureOf(args) + ":" + fileHash(src) + ":" +
                                signatureOf({clangVersionText().value_or("")});

    Job j;
    j.label = "std: precompile std.pcm";
    j.target = "std";
    j.endsTarget = true;
    j.run = [=, &console = console_] {
      return runStdModule(args, pcm, log, sig, sigText, src, console);
    };
    graph.push_back(std::move(j));
    stdJob = graph.size() - 1;
  }

  std::function<void(const NodeKey &)> plan = [&](const NodeKey &node) {
    if (outputs_.count(node) || !planned.insert(node).second)
      return;

    const TargetInfo &t = target(node);
    if (t.type == TargetType::System)
      return;
    for (const auto &dep : depsOf(node)) {
      if (target(dep).type == TargetType::Exe) {
        std::cerr << "smelt: target '" << t.name << "' depends on exe '"
                  << dep.second
                  << "'; only static_lib targets can be "
                     "dependencies\n";
        planOk = false;
        return;
      }
      plan(dep);
      if (!planOk)
        return;
    }

    const Package &pkg = ws_.packages[node.first];
    const std::string display =
        node.first == 0 ? t.name : pkg.model.project.name + "/" + t.name;
    const TargetSources &src = sources_[node.first].at(node.second);
    const Usage reqs = requirementsOf(node);
    const auto defs = resolveDefines(reqs);
    if (!defs) {
      planOk = false;
      return;
    }
    (void)defs;

    const fs::path dir = outDir(node);
    const fs::path objDir = dir / "obj";
    const fs::path modDir = dir / "modules";

    const auto info = scanNode(node);
    if (!info) {
      planOk = false;
      return;
    }
    const auto &scans = info->scans;
    const auto &providerOf = info->providerOf;

    auto importFlags = [&](const ScanRule &rule,
                           std::vector<std::string> &flags,
                           std::vector<fs::path> &inputs) -> bool {
      std::string err;
      if (!resolveImports(node, rule, flags, inputs, err)) {
        std::cerr << "smelt: " << display << ": " << err << "\n";
        return false;
      }
      return true;
    };

    std::map<std::size_t, std::size_t> precompileJob;
    std::set<std::size_t> inProgress;

    auto addImportEdges = [&](const ScanRule &rule, std::size_t job) {
      for (const auto &n : rule.needs) {
        if (n.sourcePath.empty())
          continue;
        if (providerOf.count(n.name)) {
          const std::size_t pre = precompileJob.at(providerOf.at(n.name));
          graph[pre].next.push_back(job);
          graph[job].waiting++;
        } else if (const auto it = precompileOf.find(n.name);
                   it != precompileOf.end()) {
          graph[it->second].next.push_back(job);
          graph[job].waiting++;
        }
      }
    };

    std::function<bool(std::size_t)> planInterface =
        [&](std::size_t i) -> bool {
      if (precompileJob.count(i))
        return true;
      if (!inProgress.insert(i).second) {
        std::cerr << "smelt: module import cycle in '" << t.name << "'\n";
        return false;
      }

      const auto &file = src.files[i];
      const std::string modName = scans[i].provides.front().name;

      std::vector<std::string> flags;
      std::vector<fs::path> inputs{file.path};
      if (!importFlags(scans[i], flags, inputs))
        return false;
      for (const auto &n : scans[i].needs) {
        if (n.sourcePath.empty() || !providerOf.count(n.name))
          continue;
        if (!planInterface(providerOf.at(n.name)))
          return false;
      }

      const fs::path pcm = modDir / (modName + ".pcm");
      const fs::path depFile = fs::path(pcm.string() + ".d");
      const fs::path sig = fs::path(pcm.string() + ".sig");

      std::vector<std::string> args{"clang++",
                                    "--precompile",
                                    file.path.string(),
                                    "-o",
                                    pcm.string(),
                                    stdFlag(stdEnabled_
                                                ? ws_.packages[0].model.project
                                                : pkg.model.project,
                                            true)};
      if (stdEnabled_) {
        args.push_back("-stdlib=libc++");
        args.push_back("-fmodule-file=std=" + stdPcm().string());
      }
      args.insert(args.end(), reqs.warnings.begin(), reqs.warnings.end());
      args.insert(args.end(), flags.begin(), flags.end());

      switch (profile_.optimization) {
      case Optimization::None:
        args.push_back("-O0");
        break;
      case Optimization::Speed:
        args.push_back("-O2");
        break;
      case Optimization::Size:
        args.push_back("-Os");
        break;
      }
      if (profile_.debugInfo)
        args.push_back("-g");
      for (const auto &d :
           resolveDefines(reqs).value_or(std::vector<std::string>{}))
        args.push_back("-D" + d);

      const std::string sigText = signatureOf(args) + ":" + fileHash(file.path);

      Job j;
      j.label = display + ": PCM " + modName;
      j.target = display;
      j.run = [=, &console = console_] {
        return runPrecompile(args, pcm, sig, sigText, inputs, console);
      };
      graph.push_back(std::move(j));
      const std::size_t job = graph.size() - 1;
      precompileJob[i] = job;
      precompileOf[modName] = job;

      if (stdEnabled_) {
        graph[stdJob].next.push_back(job);
        graph[job].waiting++;
      }
      addImportEdges(scans[i], job);

      inProgress.erase(i);
      return true;
    };

    for (std::size_t i = 0; i < src.files.size(); ++i) {
      if (!scans[i].provides.empty() && !planInterface(i)) {
        planOk = false;
        return;
      }
    }

    std::vector<std::size_t> compileJobs;
    std::vector<fs::path> objects;
    for (std::size_t i = 0; i < src.files.size(); ++i) {
      const auto &file = src.files[i];
      const fs::path obj = objDir / (file.relPath + ".o");
      const fs::path depFile = fs::path(obj.string() + ".d");
      const fs::path sig = fs::path(obj.string() + ".sig");
      const bool iface = file.isCxx && !scans[i].provides.empty();

      std::vector<std::string> flags;
      std::vector<fs::path> inputs;
      if (file.isCxx && !importFlags(scans[i], flags, inputs)) {
        planOk = false;
        return;
      }

      const fs::path modFile =
          iface ? modDir / (scans[i].provides.front().name + ".pcm")
                : file.path;

      // TODO: Figure out a better way to get flags.
      // I'm tired and I'm just making this little hack for now.
      std::vector<std::string> args;
      if (iface) {
        args = {"clang++", "-c",         modFile.string(),
                "-o",      obj.string(), stdFlag(pkg.model.project, true)};
        if (stdEnabled_) {
          args.push_back("-fmodule-file=std=" + stdPcm().string());
        }
        args.insert(args.end(), flags.begin(), flags.end());
        switch (profile_.optimization) {
        case Optimization::None:
          args.push_back("-O0");
          break;
        case Optimization::Speed:
          args.push_back("-O2");
          break;
        case Optimization::Size:
          args.push_back("-Os");
          break;
        }
        if (profile_.debugInfo)
          args.push_back("-g");
        for (const auto &d :
             resolveDefines(reqs).value_or(std::vector<std::string>{}))
          args.push_back("-D" + d);
      } else {
        args = compileArgs(pkg, file, reqs, obj, depFile, flags);
      }
      if (iface) {
        for (auto &a : args) {
          if (a == file.path.string())
            a = modFile.string();
        }
      }
      const std::string sigText = signatureOf(args);
      const std::string label =
          display + ": " + (file.isCxx ? "CXX " : "CC  ") + file.relPath;

      Job j;
      j.label = label;
      j.target = display;
      j.run = [=, &console = console_] {
        return runCompile(args, obj, depFile, sig, sigText, modFile, inputs,
                          console);
      };
      graph.push_back(std::move(j));
      const std::size_t job = graph.size() - 1;

      if (file.isCxx && stdEnabled_) {
        graph[stdJob].next.push_back(job);
        graph[job].waiting++;
      }
      if (iface) {
        const std::size_t pre = precompileJob.at(i);
        graph[pre].next.push_back(job);
        graph[job].waiting++;
      }
      addImportEdges(scans[i], job);

      compileJobs.push_back(job);
      objects.push_back(obj);
    }

    std::size_t outJob = 0;
    if (t.type == TargetType::StaticLib) {
      const fs::path out = dir / ("lib" + t.name + ".a");
      const fs::path sig = fs::path(out.string() + ".sig");
      std::vector<std::string> args{"ar", "rcs", out.string()};
      for (const auto &o : objects)
        args.push_back(o.string());
      const std::string sigText = signatureOf(args);

      Job j;
      j.run = [=, &console = console_] {
        return runArchive(args, out, sig, sigText, objects, console);
      };
      j.label = display + ": AR  lib" + t.name + ".a";
      j.target = display;
      j.endsTarget = true;
      graph.push_back(std::move(j));
      outJob = graph.size() - 1;
      outputPath[node] = out;
    } else {
      std::set<NodeKey> seen{node};
      std::vector<NodeKey> libs;
      staticLibs(node, seen, libs);
      std::reverse(libs.begin(), libs.end());

      std::vector<fs::path> archives;
      std::vector<std::size_t> libJobs;
      for (const auto &lib : libs) {
        archives.push_back(outputPath.count(lib) ? outputPath.at(lib)
                                                 : outputs_.at(lib));
        if (producer.count(lib))
          libJobs.push_back(producer.at(lib));
      }

      const fs::path out = dir / t.name;
      const fs::path sig = fs::path(out.string() + ".sig");
      std::vector<std::string> args{"clang++"};
      if (profile_.debugInfo)
        args.push_back("-g");
      if (stdEnabled_)
        args.push_back("-stdlib=libc++");
      args.push_back("-o");
      args.push_back(out.string());
      for (const auto &o : objects)
        args.push_back(o.string());
      for (const auto &a : archives)
        args.push_back(a.string());
      args.insert(args.end(), reqs.link.begin(), reqs.link.end());

      std::vector<fs::path> inputs = objects;
      inputs.insert(inputs.end(), archives.begin(), archives.end());
      const std::string sigText = signatureOf(args);

      Job j;
      j.run = [=, &console = console_] {
        return runLink(args, out, sig, sigText, inputs, console);
      };
      j.label = display + ": LD  " + t.name;
      j.target = display;
      j.endsTarget = true;
      graph.push_back(std::move(j));
      outJob = graph.size() - 1;
      for (const auto jobIndex : libJobs) {
        graph[outJob].waiting++;
        graph[jobIndex].next.push_back(outJob);
      }
      outputPath[node] = out;
    }

    for (const auto c : compileJobs) {
      graph[outJob].waiting++;
      graph[c].next.push_back(outJob);
    }
    producer[node] = outJob;
  };

  for (const auto &r : roots) {
    plan(r);
    if (!planOk)
      return std::nullopt;
  }

  if (!planOk)
    return std::nullopt;

  console_.setTotal(graph.size());

  std::mutex mu;
  std::condition_variable cv;
  std::deque<std::size_t> ready;
  std::size_t finished = 0;
  bool failed = false;

  for (std::size_t i = 0; i < graph.size(); ++i) {
    if (graph[i].waiting == 0)
      ready.push_back(i);
  }

  using Clock = std::chrono::steady_clock;
  std::map<std::string, Clock::time_point> started;
  std::map<std::string, bool> builtAny;

  auto worker = [&] {
    while (true) {
      std::size_t job = 0;
      {
        std::unique_lock lock(mu);
        cv.wait(lock, [&] {
          return !ready.empty() || finished == graph.size() || failed;
        });
        if (failed || (ready.empty() && finished == graph.size()))
          return;
        if (ready.empty())
          continue;
        job = ready.front();
        ready.pop_front();
        started.emplace(graph[job].target, Clock::now());
      }

      console_.startJob(graph[job].label);
      const Outcome outcome = graph[job].run();

      std::lock_guard lock(mu);
      ++finished;
      if (outcome == Outcome::Failed) {
        console_.failJob(graph[job].label);
        failed = true;
      } else {
        const Job &j = graph[job];
        if (outcome == Outcome::Built)
          builtAny[j.target] = true;
        console_.finishJob(j.label, outcome == Outcome::Built);
        if (j.endsTarget) {
          const double secs =
              std::chrono::duration<double>(Clock::now() - started[j.target])
                  .count();
          console_.finishTarget(j.target, secs, builtAny[j.target]);
        }
        for (const auto next : graph[job].next) {
          if (--graph[next].waiting == 0)
            ready.push_back(next);
        }
      }
      cv.notify_all();
    }
  };

  const std::size_t n = std::max<std::size_t>(1, jobs);
  std::vector<std::thread> pool;
  for (std::size_t i = 0; i < n; ++i)
    pool.emplace_back(worker);
  for (auto &th : pool)
    th.join();

  if (failed)
    return std::nullopt;

  console_.finish();

  for (const auto &[node, path] : outputPath)
    outputs_[node] = path;

  std::map<NodeKey, fs::path> result;
  for (const auto &r : roots)
    result[r] = outputPath.count(r) ? outputPath.at(r) : outputs_.at(r);

  return result;
}

auto Builder::build(const NodeKey &node, const std::size_t jobs)
    -> std::optional<fs::path> {
  const auto all = buildSet({node}, jobs);
  if (!all)
    return std::nullopt;
  return all->at(node);
}
} // namespace smelt

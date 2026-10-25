#include "build.hpp"
#include "discover.hpp"
#include "process.hpp"
#include "sema.hpp"
#include "workspace.hpp"
#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
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

auto hashHex(const std::string &text) -> std::string {
  std::uint64_t h = 0xCBF29CE484222325; // FNV-1a
  for (const unsigned char c : text) {
    h ^= c;
    h *= 0x00000100000001B3;
  }
  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << h;
  return out.str();
}

auto signatureOf(const std::vector<std::string> &args) -> std::string {
  std::string joined;
  for (const auto &a : args) {
    joined += a;
    joined += '\x1f';
  }
  return hashHex(joined);
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
                 const int rank) -> Builder::Define {
  Builder::Define d;
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
} // namespace

Builder::Builder(const Workspace &ws, const std::vector<SourceMap> &sources,
                 ProfileInfo profile)
    : ws_(ws), sources_(sources), profile_(std::move(profile)) {}

auto Builder::target(const NodeKey &n) const -> const TargetInfo & {
  return *findTarget(ws_.packages[n.first], n.second);
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
    u.includes.push_back((p.root / d).string());

  for (const auto &raw : t.defines) {
    Define def = parseDefine(raw, owner, 3);
    u.defines.push_back(std::move(def));
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
  u.includes.push_back(sources_[n.first].at(n.second).root.string());
  for (const auto &d : t.privateIncludes)
    u.includes.push_back((p.root / d).string());

  for (const auto &raw : profile_.defines) {
    u.defines.push_back(parseDefine(raw, "profile " + profile_.name, 1));
  }

  const std::string owner = p.model.project.name + ":" + t.name;
  for (const auto &raw : t.defines)
    u.defines.push_back(parseDefine(raw, owner, 2));

  addPublic(n, u);
  std::set<NodeKey> seen{n};
  walkDeps(n, seen, u);

  dedupe(u.includes);
  dedupe(u.options);
  dedupe(u.link);
  return u;
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
                          const std::filesystem::path &dep) const
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
      stdFlag(pkg.model.project, src.isCxx),
  };

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
  for (const auto &i : u.includes)
    args.push_back("-I" + i);

  return args;
}

auto Builder::resolveDefines(const Usage &u) const
    -> std::optional<std::vector<std::string>> {
  std::map<std::string, std::map<std::string, Define>> bySource;
  std::map<std::string, std::size_t> sourceOrder;

  for (const auto &d : u.defines) {
    bySource[d.source][d.name] = d;
    sourceOrder.emplace(d.source, sourceOrder.size());
  }

  std::vector<Define> sourcesFlat;
  for (const auto &[source, macros] : bySource) {
    for (const auto &[name, def] : macros)
      sourcesFlat.push_back(def);
  }
  std::stable_sort(
      sourcesFlat.begin(), sourcesFlat.end(),
      [](const Define &a, const Define &b) { return a.rank < b.rank; });

  std::map<std::string, Define> merged;
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
    const Define &d = merged.at(name);
    out.push_back(d.hasValue ? d.name + "=" + d.value : d.name);
  }
  return out;
}

auto Builder::compileOne(const Package &pkg, const SourceFile &src,
                         const Usage &u, const std::filesystem::path &objDir)
    -> std::optional<fs::path> {
  const fs::path obj = objDir / (src.relPath + ".o");
  const fs::path dep = fs::path(obj.string() + ".d");
  const fs::path sig = fs::path(obj.string() + ".sig");

  const auto args = compileArgs(pkg, src, u, obj, dep);
  const std::string sigText = signatureOf(args);

  const auto deps = readDepFile(dep);
  std::vector<fs::path> inputs{src.path};
  if (deps) {
    for (const auto &d : *deps)
      inputs.emplace_back(d);
  }

  if (deps && !isStale(obj, sig, sigText, inputs))
    return obj;

  fs::create_directories(obj.parent_path());
  fs::remove(sig);

  std::cout << "  " << (src.isCxx ? "CXX " : "CC  ") << pkg.model.project.name
            << "/" << src.relPath << "\n";
  if (runProcess(args) != 0) {
    std::cerr << "smelt: compile failed: " << joinCommand(args) << "\n";
    return std::nullopt;
  }

  writeSignature(sig, sigText);
  return obj;
}

auto Builder::archive(const std::string &name, const fs::path &dir,
                      const std::vector<fs::path> &objects)
    -> std::optional<fs::path> {
  const fs::path out = dir / ("lib" + name + ".a");
  const fs::path sig = fs::path(out.string() + ".sig");

  std::vector<std::string> args{"ar", "rcs", out.string()};
  for (const auto &o : objects)
    args.push_back(o.string());

  const std::string sigText = signatureOf(args);
  if (!isStale(out, sig, sigText, objects))
    return out;

  fs::create_directories(dir);
  fs::remove(out);
  fs::remove(sig);

  std::cout << "  AR  lib" << name << ".a\n";
  if (runProcess(args) != 0) {
    std::cerr << "smelt: archive failed: " << joinCommand(args) << "\n";
    return std::nullopt;
  }

  writeSignature(sig, sigText);
  return out;
}

auto Builder::link(const std::string &name, const fs::path &dir,
                   const std::vector<fs::path> &objects,
                   const std::vector<fs::path> &archives,
                   const std::vector<std::string> &flags)
    -> std::optional<fs::path> {
  const fs::path out = dir / name;
  const fs::path sig = fs::path(out.string() + ".sig");

  std::vector<std::string> args{"clang++"};
  if (profile_.debugInfo)
    args.push_back("-g");
  args.push_back("-o");
  args.push_back(out.string());
  for (const auto &o : objects)
    args.push_back(o.string());
  for (const auto &a : archives)
    args.push_back(a.string());
  args.insert(args.end(), flags.begin(), flags.end());

  std::vector<fs::path> inputs = objects;
  inputs.insert(inputs.end(), archives.begin(), archives.end());

  const std::string sigText = signatureOf(args);
  if (!isStale(out, sig, sigText, inputs))
    return out;

  fs::create_directories(dir);
  fs::remove(out);
  fs::remove(sig);

  std::cout << "  LD  " << name << "\n";
  if (runProcess(args) != 0) {
    std::cerr << "smelt: link failed: " << joinCommand(args) << "\n";
    return std::nullopt;
  }

  writeSignature(sig, sigText);
  return out;
}

auto Builder::build(const NodeKey &node) -> std::optional<fs::path> {
  if (const auto it = outputs_.find(node); it != outputs_.end())
    return it->second;

  if (!active_.insert(node).second) {
    std::cerr << "smelt: internal error: cycle through target '" << node.second
              << "'\n";
    return std::nullopt;
  }

  const Package &pkg = ws_.packages[node.first];
  const TargetInfo &t = target(node);

  for (const auto &dep : depsOf(node)) {
    if (target(dep).type == TargetType::Exe) {
      std::cerr << "smelt: target '" << t.name << "' depends on exe '"
                << dep.second
                << "'; only static_lib targets can be dependencies\n";
      return std::nullopt;
    }
    if (!build(dep))
      return std::nullopt;
  }

  const TargetSources &src = sources_[node.first].at(node.second);
  const Usage reqs = requirementsOf(node);
  if (!resolveDefines(reqs)) {
    return std::nullopt;
  }
  const fs::path dir = outDir(node);
  const fs::path objDir = dir / "obj";

  std::vector<fs::path> objects;
  for (const auto &file : src.files) {
    const auto obj = compileOne(pkg, file, reqs, objDir);
    if (!obj)
      return std::nullopt;
    objects.push_back(*obj);
  }

  std::optional<fs::path> out;
  if (t.type == TargetType::StaticLib) {
    out = archive(t.name, dir, objects);
  } else {
    std::set<NodeKey> seen{node};
    std::vector<NodeKey> libs;
    staticLibs(node, seen, libs);
    std::reverse(libs.begin(), libs.end());

    std::vector<fs::path> archives;
    for (const auto &lib : libs) {
      const auto a = build(lib);
      if (!a)
        return std::nullopt;
      archives.push_back(*a);
    }
    out = link(t.name, dir, objects, archives, reqs.link);
  }

  if (!out)
    return std::nullopt;
  outputs_[node] = *out;
  active_.erase(node);
  return out;
}

} // namespace smelt

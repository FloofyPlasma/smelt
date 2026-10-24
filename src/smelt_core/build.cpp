#include "build.hpp"
#include "discover.hpp"
#include "process.hpp"
#include "sema.hpp"
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
} // namespace

Builder::Builder(const Model &model, const SourceMap &sources,
                 fs::path projectRoot, ProfileInfo profile)
    : model_(model), sources_(sources), root_(std::move(projectRoot)),
      profile_(std::move(profile)) {}

auto Builder::find(const std::string &name) const -> const TargetInfo * {
  for (const auto &t : model_.targets) {
    if (t.name == name)
      return &t;
  }
  return nullptr;
}

auto Builder::targetDir(const std::string &name) const -> fs::path {
  return root_ / "build" / profile_.name / name;
}

auto Builder::stdFlag(const bool cxx) const -> std::string {
  if (cxx) {
    const int s = model_.project.cxxStd.value_or(20);
    return "-std=c++" + std::to_string(s);
  }
  const int s = model_.project.cStd.value_or(17);
  return "-std=c" + std::to_string(s);
}

void Builder::collectPublicIncludes(const std::string &name,
                                    std::set<std::string> &visited,
                                    std::vector<std::string> &out) const {
  if (!visited.insert(name).second)
    return;
  const TargetInfo *t = find(name);
  if (!t)
    return;
  for (const auto &dir : t->publicIncludes)
    out.push_back((root_ / dir).string());
  for (const auto &dep : t->depends)
    collectPublicIncludes(dep.name, visited, out);
}

auto Builder::includeDirs(const TargetInfo &t, const TargetSources &src) const
    -> std::vector<std::string> {
  std::vector<std::string> dirs{src.root.string()};
  for (const auto &d : t.privateIncludes)
    dirs.push_back((root_ / d).string());
  for (const auto &d : t.publicIncludes)
    dirs.push_back((root_ / d).string());

  std::set<std::string> visited{t.name};
  for (const auto &dep : t.depends)
    collectPublicIncludes(dep.name, visited, dirs);

  std::vector<std::string> unique;
  for (const auto &d : dirs) {
    if (std::find(unique.begin(), unique.end(), d) == unique.end())
      unique.push_back(d);
  }
  return unique;
}

void Builder::collectLibs(const std::string &name, std::set<std::string> &seen,
                          std::vector<std::string> &post) const {
  if (!seen.insert(name).second)
    return;
  const TargetInfo *t = find(name);
  if (!t)
    return;
  for (const auto &dep : t->depends)
    collectLibs(dep.name, seen, post);
  if (t->type == TargetType::StaticLib)
    post.push_back(name);
}

auto Builder::compileArgs(const TargetInfo &t, const SourceFile &src,
                          const std::vector<std::string> &includes,
                          const std::filesystem::path &obj,
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
      stdFlag(src.isCxx),
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

  for (const auto &d : t.defines)
    args.push_back("-D" + d);
  args.insert(args.end(), t.compileOptions.begin(), t.compileOptions.end());
  for (const auto &i : includes)
    args.push_back("-I" + i);

  return args;
}

auto Builder::compileOne(const TargetInfo &t, const SourceFile &src,
                         const std::vector<std::string> &includes,
                         const std::filesystem::path &objDir) const
    -> std::optional<fs::path> {
  const fs::path obj = objDir / (src.relPath + ".o");
  const fs::path dep = fs::path(obj.string() + ".d");
  const fs::path sig = fs::path(obj.string() + ".sig");

  const auto args = compileArgs(t, src, includes, obj, dep);
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

  std::cout << "  " << (src.isCxx ? "CXX " : "CC  ") << src.relPath << "\n";
  if (runProcess(args) != 0) {
    std::cerr << "smelt: compile failed: " << joinCommand(args) << "\n";
    return std::nullopt;
  }

  writeSignature(sig, sigText);
  return obj;
}

auto Builder::archive(const TargetInfo &t,
                      const std::vector<std::filesystem::path> &objects)
    -> std::optional<fs::path> {
  const fs::path dir = targetDir(t.name);
  const fs::path out = dir / ("lib" + t.name + ".a");
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

  std::cout << "  AR  lib" << t.name << ".a\n";
  if (runProcess(args) != 0) {
    std::cerr << "smelt: archive failed: " << joinCommand(args) << "\n";
    return std::nullopt;
  }

  writeSignature(sig, sigText);
  return out;
}

auto Builder::link(const TargetInfo &t, const std::vector<fs::path> &objects,
                   const std::vector<fs::path> &archives,
                   const std::vector<std::string> &flags)
    -> std::optional<fs::path> {
  const fs::path dir = targetDir(t.name);
  const fs::path out = dir / t.name;
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
  inputs.insert(inputs.end(), flags.begin(), flags.end());

  const std::string sigText = signatureOf(args);
  if (!isStale(out, sig, sigText, inputs))
    return out;

  fs::create_directories(dir);
  fs::remove(out);
  fs::remove(sig);

  std::cout << "  LD  " << t.name << "\n";
  if (runProcess(args) != 0) {
    std::cerr << "smelt: link failed: " << joinCommand(args) << "\n";
    return std::nullopt;
  }

  writeSignature(sig, sigText);
  return out;
}

auto Builder::build(const std::string &name) -> std::optional<fs::path> {
  if (const auto it = outputs_.find(name); it != outputs_.end())
    return it->second;

  const TargetInfo *t = find(name);
  if (!t) {
    std::cerr << "smelt: unknown target '" << name << "'\n";
    return std::nullopt;
  }

  for (const auto &dep : t->depends) {
    const TargetInfo *d = find(dep.name);
    if (d && d->type == TargetType::Exe) {
      std::cerr << "smelt: target '" << name << "' depends on exe '" << dep.name
                << "'; only static_lib targets can be dependencies\n";
      return std::nullopt;
    }
    if (!build(dep.name))
      return std::nullopt;
  }

  const auto srcIt = sources_.find(name);
  if (srcIt == sources_.end()) {
    std::cerr << "smelt: no sources for target '" << name << "'\n";
    return std::nullopt;
  }
  const TargetSources &src = srcIt->second;

  const fs::path objDir = targetDir(name) / "obj";
  const auto includes = includeDirs(*t, src);

  std::vector<fs::path> objects;
  for (const auto &file : src.files) {
    const auto obj = compileOne(*t, file, includes, objDir);
    if (!obj)
      return std::nullopt;
    objects.push_back(*obj);
  }

  std::optional<fs::path> out;
  if (t->type == TargetType::StaticLib) {
    out = archive(*t, objects);
  } else {
    std::vector<std::string> libs;
    std::set<std::string> seen{name};
    for (const auto &dep : t->depends)
      collectLibs(dep.name, seen, libs);
    std::reverse(libs.begin(), libs.end());

    std::vector<fs::path> archives;
    std::vector<std::string> flags = t->link;
    for (const auto &lib : libs) {
      archives.push_back(outputs_.at(lib));
      const TargetInfo *lt = find(lib);
      flags.insert(flags.end(), lt->link.begin(), lt->link.end());
    }
    out = link(*t, objects, archives, flags);
  }

  if (!out)
    return std::nullopt;
  outputs_[name] = *out;
  return out;
}

} // namespace smelt

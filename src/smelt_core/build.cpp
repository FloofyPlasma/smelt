#include "build.hpp"
#include "console.hpp"
#include "discover.hpp"
#include "process.hpp"
#include "sema.hpp"
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
                   Console &console) {
  const fs::path log = fs::path(obj.string() + ".log");

  const auto deps = readDepFile(dep);
  std::vector<fs::path> inputs{source};
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

  addPublic(n, u);
  std::set<NodeKey> seen{n};
  walkDeps(n, seen, u);

  dedupe(u.includes);
  dedupe(u.systemIncludes);
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
      "-Wall",
      "-Wextra",
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

[[nodiscard]] auto
Builder::compileUnits(const std::vector<std::string> &rootTargets)
    -> std::optional<std::vector<CompileUnit>> {
  std::vector<CompileUnit> out;
  std::set<NodeKey> visited;

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

    const fs::path objDir = outDir(node) / "obj";
    for (const auto &file : src.files) {
      const fs::path obj = objDir / (file.relPath + ".o");
      const fs::path dep = fs::path(obj.string() + ".d");
      out.push_back({ws_.packages[0].root, file.path, obj,
                     compileArgs(pkg, file, reqs, obj, dep)});
    }
    return true;
  };

  for (const auto &name : rootTargets) {
    if (!visit({0, name}))
      return std::nullopt;
  }
  return out;
}

auto Builder::build(const NodeKey &root, const std::size_t jobs)
    -> std::optional<fs::path> {
  std::deque<Job> graph;
  std::map<NodeKey, std::size_t> producer;
  std::map<NodeKey, fs::path> outputPath;
  std::set<NodeKey> planned;
  bool planOk = true;

  if (const auto it = outputs_.find(root); it != outputs_.end())
    return it->second;

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

    std::vector<std::size_t> compileJobs;
    std::vector<fs::path> objects;
    for (const auto &file : src.files) {
      const fs::path obj = objDir / (file.relPath + ".o");
      const fs::path depFile = fs::path(obj.string() + ".d");
      const fs::path sig = fs::path(obj.string() + ".sig");
      const auto args = compileArgs(pkg, file, reqs, obj, depFile);
      const std::string sigText = signatureOf(args);
      const std::string label = display + ": " +
                                std::string(file.isCxx ? "CXX " : "CC  ") +
                                file.relPath;

      Job j;
      j.run = [=, &console = console_] {
        return runCompile(args, obj, depFile, sig, sigText, file.path, console);
      };
      j.label = label;
      j.target = display;
      graph.push_back(std::move(j));
      compileJobs.push_back(graph.size() - 1);
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

  plan(root);
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

  return outputPath.at(root);
}
} // namespace smelt

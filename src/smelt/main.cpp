#include "build.hpp"
#include "compdb.hpp"
#include "diagnostics.hpp"
#include "discover.hpp"
#include "fetch.hpp"
#include "install.hpp"
#include "process.hpp"
#include "sema.hpp"
#include "tools.hpp"
#include "workspace.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {
struct Options {
  std::string command;
  std::string profile = "debug";
  std::string updateName;
  std::string prefix;
  std::optional<std::string> target;
  std::vector<std::string> runArgs;
  std::size_t jobs =
      std::max<std::size_t>(1, std::thread::hardware_concurrency());
};

void usage() {
  std::cerr << "usage:\n"
               "  smelt build [--profile NAME] [--target NAME] [--jobs N]\n"
               "  smelt run   [--profile NAME] [--target NAME] [-- ARGS...]\n"
               "  smelt update [NAME]\n"
               "  smelt install   [--prefix DIR]\n"
               "  smelt uninstall [--prefix DIR]\n"
               "  smelt clean\n"
               "  smelt fetch\n"
               "  smelt compdb [--profile NAME]\n";
}

auto parseOptions(int argc, char **argv, Options &o) -> bool {
  if (argc < 2)
    return false;
  o.command = argv[1];

  const bool known = o.command == "build" || o.command == "run" ||
                     o.command == "clean" || o.command == "fetch" ||
                     o.command == "update" || o.command == "compdb" ||
                     o.command == "install" || o.command == "uninstall";

  if (!known)
    return false;

  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--") {
      for (int j = i + 1; j < argc; ++j)
        o.runArgs.emplace_back(argv[j]);
      break;
    }
    if (arg == "--profile" && i + 1 < argc) {
      o.profile = argv[++i];
    } else if (arg == "--prefix" && i + 1 < argc &&
               (o.command == "install" || o.command == "uninstall")) {
      o.prefix = argv[++i];
    } else if ((arg == "--jobs" && i + 1 < argc) &&
               (o.command == "build" || o.command == "run")) {
      const std::string value = argv[++i];
      try {
        o.jobs = std::stoul(value);
      } catch (const std::exception &) {
        return false;
      }
      if (o.jobs == 0)
        return false;
    } else if (arg == "--target" && i + 1 < argc) {
      o.target = argv[++i];
    } else if (o.command == "update" && !arg.empty() && arg[0] != '-' &&
               o.updateName.empty()) {
      o.updateName = arg;
    } else {
      return false;
    }
  }
  if (o.command != "run" && !o.runArgs.empty())
    return false;
  return true;
}

void printDiagnostics(const smelt::Diagnostics &diags) {
  for (const auto &d : diags.items()) {
    std::cerr << smelt::Diagnostics::format(d) << "\n";
  }
}

std::vector<std::string> rootTargetNames(const smelt::Workspace &ws) {
  std::vector<std::string> names;
  for (const auto &t : ws.packages[0].model.targets)
    names.push_back(t.name);
  return names;
}

void printOutput(const smelt::TargetInfo &t, const fs::path &out,
                 const fs::path &root) {
  const std::string kind = t.type == smelt::TargetType::Exe ? "exe" : "lib";
  std::cout << "  " << kind << "  " << fs::relative(out, root).string() << "\n";
}

std::vector<std::string> toolsFor(const Options &o, bool hasRemoteDeps) {
  std::vector<std::string> tools;
  if (o.command == "build" || o.command == "run" || o.command == "install") {
    tools = {"clang", "clang++", "ar"};
  }
  if (o.command == "fetch" || o.command == "update" || hasRemoteDeps) {
    tools.push_back("git");
  }
  return tools;
}
} // namespace

int main(int argc, char **argv) {
  ::setenv("GIT_TERMINAL_PROMPT", "0", 1);

  Options opts;
  if (!parseOptions(argc, argv, opts)) {
    usage();
    return 2;
  }

  const auto started = std::chrono::steady_clock::now();

  const fs::path root = fs::canonical(fs::current_path());
  const std::string projectFile = "project.smelt";

  if (opts.command == "clean") {
    std::error_code ec;
    fs::remove_all(root / "build", ec);
    if (ec) {
      std::cerr << "smelt: cannot remove build/:" << ec.message() << "\n";
      return 1;
    }
    std::cout << "cleaned build/\n";
    return 0;
  }

  if (opts.command == "install" || opts.command == "uninstall") {
    fs::path prefix;
    if (!opts.prefix.empty()) {
      prefix = fs::path(opts.prefix);
    } else if (const char *home = std::getenv("HOME"); home && *home) {
      prefix = fs::path(home) / ".local";
    } else {
      std::cerr << "smelt: HOME is not set; use --prefix\n";
      return 2;
    }
    if (!smelt::requireTools(toolsFor(opts, false)))
      return 2;
    return opts.command == "install" ? smelt::installSelf(root, prefix)
                                     : smelt::uninstallSelf(prefix);
  }

  if (opts.command == "fetch" || opts.command == "update") {
    if (!smelt::requireTools(toolsFor(opts, true)))
      return 2;
    smelt::Diagnostics fetchDiags("project.smelt");
    smelt::FetchOptions fo;
    fo.update = opts.command == "update";
    if (fo.update && !opts.updateName.empty())
      fo.name = opts.updateName;
    if (!smelt::fetchDependencies(root, fetchDiags, fo)) {
      printDiagnostics(fetchDiags);
      return 1;
    }
    return 0;
  }

  smelt::Diagnostics diags("project.smelt");
  auto ws = smelt::Workspace::load(root, diags);
  if (!ws) {
    printDiagnostics(diags);
    return 1;
  }
  bool hasRemote = false;
  for (const auto &pkg : ws->packages) {
    for (const auto &dep : pkg.model.dependencies) {
      if (dep.git || dep.registry)
        hasRemote = true;
    }
  }
  if (!smelt::requireTools(toolsFor(opts, hasRemote)))
    return 2;

  std::optional<smelt::ProfileInfo> profile;
  for (const auto &p : ws->packages[0].model.profiles) {
    if (p.name == opts.profile)
      profile = p;
  }
  if (!profile) {
    std::cerr << "smelt: unknown profile '" << opts.profile << "'\n";
    return 2;
  }

  std::vector<smelt::SourceMap> sources;
  for (const auto &pkg : ws->packages) {
    diags.setFile(pkg.projectFile);
    auto map = smelt::discoverSources(pkg.model, pkg.root, diags);
    if (!map) {
      printDiagnostics(diags);
      return 1;
    }
    sources.push_back(std::move(*map));
  }

  if (!smelt::checkOwnership(sources, *ws, diags)) {
    printDiagnostics(diags);
    return 1;
  }

  smelt::Builder builder(*ws, sources, *profile);
  const auto &rootTargets = ws->packages[0].model.targets;

  if (opts.command == "compdb") {
    const auto units = builder.compileUnits(rootTargetNames(*ws));
    if (!units)
      return 1;
    if (!writeCompileDatabase(root / "compile_commands.json", *units)) {
      std::cerr << "smelt: cannot write compile_commands.json\n";
      return 1;
    }
    std::cout << "wrote compile_commands.json (" << units->size()
              << " entries)\n";
    return 0;
  }

  if (opts.command == "build") {
    std::vector<std::string> names;
    if (opts.target) {
      if (!smelt::findTarget(ws->packages[0], *opts.target)) {
        std::cerr << "smelt: unknown target '" << *opts.target << "'\n";
        return 1;
      }
      names.push_back(*opts.target);
    } else {
      for (const auto &t : rootTargets)
        names.push_back(t.name);
    }

    for (const auto &name : names) {
      const auto out = builder.build(smelt::NodeKey{0, name}, opts.jobs);
      if (!out)
        return 1;
      printOutput(*smelt::findTarget(ws->packages[0], name), *out, root);
    }

    const double secs = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
    std::cout << std::fixed << std::setprecision(1) << "build ok ("
              << profile->name << ") in " << secs << "s\n";
    return 0;
  }

  std::string name;
  if (opts.target) {
    name = *opts.target;
  } else {
    const auto exe = std::find_if(rootTargets.begin(), rootTargets.end(),
                                  [](const smelt::TargetInfo &t) {
                                    return t.type == smelt::TargetType::Exe;
                                  });
    if (exe == rootTargets.end()) {
      std::cerr << "smelt: no executable target in project\n";
      return 1;
    }
    name = exe->name;
  }

  const smelt::TargetInfo *selected = smelt::findTarget(ws->packages[0], name);
  if (!selected) {
    std::cerr << "smelt: unknown target '" << name << "'\n";
    return 1;
  }
  if (selected->type != smelt::TargetType::Exe) {
    std::cerr << "smelt: target '" << name << "' is not an executable\n";
    return 1;
  }

  const auto exePath = builder.build(smelt::NodeKey{0, name}, opts.jobs);
  if (!exePath)
    return 1;

  std::vector<std::string> runArgv{exePath->string()};
  runArgv.insert(runArgv.end(), opts.runArgs.begin(), opts.runArgs.end());

  const int code = smelt::runProcess(runArgv);
  return code < 0 ? 1 : code;
}

#include "build.hpp"
#include "compdb.hpp"
#include "diagnostics.hpp"
#include "discover.hpp"
#include "fetch.hpp"
#include "process.hpp"
#include "sema.hpp"
#include "workspace.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {
struct Options {
  std::string command;
  std::string profile = "debug";
  std::optional<std::string> target;
  std::vector<std::string> runArgs;
};

void usage() {
  std::cerr << "usage:\n"
               "  smelt build [--profile NAME] [--target NAME]\n"
               "  smelt run   [--profile NAME] [--target NAME] [-- ARGS...]\n"
               "  smelt clean\n"
               "  smelt fetch\n"
               "  smelt compdb [--profile NAME]\n";
}

auto parseOptions(int argc, char **argv, Options &o) -> bool {
  if (argc < 2)
    return false;
  o.command = argv[1];
  if (o.command != "build" && o.command != "run" && o.command != "clean" &&
      o.command != "fetch" && o.command != "compdb")
    return false;

  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--") {
      for (int j = i + 1; j < argc; j++)
        o.runArgs.emplace_back(argv[j]);
      break;
    }
    if (arg == "--profile" && i + 1 < argc) {
      o.profile = argv[++i];
    } else if (arg == "--target" && i + 1 < argc) {
      o.target = argv[++i];
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
} // namespace

int main(int argc, char **argv) {
  ::setenv("GIT_TERMINAL_PROMPT", "0", 1);

  Options opts;
  if (!parseOptions(argc, argv, opts)) {
    usage();
    return 2;
  }

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

  if (opts.command == "fetch") {
    smelt::Diagnostics fetchDiags("project.smelt");
    if (!smelt::fetchDependencies(root, fetchDiags)) {
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
    std::vector<std::string> warnings;
    auto map = smelt::discoverSources(pkg.model, pkg.root, diags, warnings);
    for (const auto &w : warnings)
      std::cerr << pkg.projectFile << ": warning: " << w << "\n";
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
    if (opts.target) {
      if (!smelt::findTarget(ws->packages[0], *opts.target)) {
        std::cerr << "smelt: unknown target '" << *opts.target << "'\n";
        return 1;
      }
      if (!builder.build(smelt::NodeKey{0, *opts.target}))
        return 1;
    } else {
      for (const auto &t : rootTargets) {
        if (!builder.build(smelt::NodeKey{0, t.name}))
          return 1;
      }
    }
    std::cout << "build ok (" << profile->name << ")\n";
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

  const auto exePath = builder.build(smelt::NodeKey{0, name});
  if (!exePath)
    return 1;

  std::vector<std::string> runArgv{exePath->string()};
  runArgv.insert(runArgv.end(), opts.runArgs.begin(), opts.runArgs.end());

  const int code = smelt::runProcess(runArgv);
  return code < 0 ? 1 : code;
}

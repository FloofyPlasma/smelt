#include "build.hpp"
#include "compdb.hpp"
#include "diagnostics.hpp"
#include "discover.hpp"
#include "fetch.hpp"
#include "init.hpp"
#include "install.hpp"
#include "process.hpp"
#include "sema.hpp"
#include "tools.hpp"
#include "tree.hpp"
#include "workspace.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#ifndef SMELT_VERSION
#define SMELT_VERSION "unknown"
#endif

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

struct CommandSpec {
  std::string_view name;
  std::string_view positional;
  bool passthrough;
  std::string_view summary;
};

struct OptionSpec {
  std::string_view name;
  std::string_view value;
  std::string_view commands;
  std::string_view help;
};

constexpr CommandSpec kCommands[] = {
    {"build", "", false, "build the root project's targets"},
    {"run", "", true, "build and run an executable target"},
    {"clean", "", false, "remove build/"},
    {"fetch", "", false, "resolve dependencies and write smelt.lock"},
    {"update", "[NAME]", false, "move locked dependencies forward"},
    {"tree", "", false, "show the dependency graph"},
    {"check", "", false, "parse and check project.smelt"},
    {"init", "", false, "create a project interactively"},
    {"compdb", "", false, "write compile_commands.json"},
    {"install", "", false, "build and install smelt into PREFIX/bin"},
    {"uninstall", "", false, "remove the installed smelt"},
};

constexpr OptionSpec kOptions[] = {
    {"--profile", "NAME", "build run compdb", "build profile"},
    {"--target", "NAME", "build run", "build or run only this target"},
    {"--jobs", "N", "build run", "parallel jobs, 1 to 4096 (default: cores)"},
    {"--prefix", "DIR", "install uninstall",
     "install prefix (default: ~/.local)"},
};

enum class Parse { Run, Help, Version, Error };

auto findCommand(std::string_view name) -> const CommandSpec * {
  for (const auto &c : kCommands) {
    if (c.name == name)
      return &c;
  }
  return nullptr;
}

auto findOption(std::string_view name) -> const OptionSpec * {
  for (const auto &o : kOptions) {
    if (o.name == name)
      return &o;
  }
  return nullptr;
}

auto appliesTo(const OptionSpec &opt, std::string_view cmd) -> bool {
  std::size_t start = 0;
  while (start <= opt.commands.size()) {
    const std::size_t end = opt.commands.find(' ', start);
    const std::string_view word = opt.commands.substr(
        start,
        end == std::string_view::npos ? std::string_view::npos : end - start);

    if (word == cmd)
      return true;
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  return false;
}

auto parseJobs(std::string_view value, std::string &err)
    -> std::optional<std::size_t> {
  if (value.empty() || value.size() > 5 ||
      value.find_first_not_of("0123456789") != std::string_view::npos) {
    err = "'--jobs' needs a whole number, found '" + std::string(value) + "'";
    return std::nullopt;
  }
  const std::size_t n = std::stoul(std::string(value));
  if (n == 0) {
    err = "'--jobs' must be at least 1, found '0'";
    return std::nullopt;
  }
  if (n > 4096) {
    err = "'--jobs' must be at most 4096, found '" + std::string(value) + "'";
    return std::nullopt;
  }
  return n;
}

auto applyValue(Options &o, const OptionSpec &opt, std::string_view value,
                std::string &err) -> bool {
  if (opt.name == "--profile") {
    o.profile = std::string(value);
  } else if (opt.name == "--target") {
    o.target = std::string(value);
  } else if (opt.name == "--prefix") {
    o.prefix = std::string(value);
  } else if (opt.name == "--jobs") {
    const auto n = parseJobs(value, err);
    if (!n)
      return false;
    o.jobs = *n;
  }
  return true;
}

auto parseArgs(int argc, char **argv, Options &o, std::string &err) -> Parse {
  std::vector<std::string_view> args;
  for (int i = 1; i < argc; ++i)
    args.emplace_back(argv[i]);

  if (!args.empty() && !args[0].starts_with('-'))
    o.command = std::string(args[0]);

  for (const auto a : args) {
    if (a == "--")
      break;
    if (a == "-h" || a == "--help")
      return Parse::Help;
    if (a == "--version")
      return Parse::Version;
  }

  if (args.empty()) {
    err = "missing command";
    return Parse::Error;
  }
  if (args[0].starts_with('-')) {
    err = "expected a command before '" + std::string(args[0]) + "'";
    return Parse::Error;
  }
  const CommandSpec *cmd = findCommand(args[0]);
  if (cmd == nullptr) {
    err = "unknown command '" + std::string(args[0]) + "'";
    return Parse::Error;
  }
  o.command = std::string(cmd->name);

  std::vector<std::string_view> seen;
  std::size_t positionals = 0;

  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string_view a = args[i];

    if (a == "--") {
      if (!cmd->passthrough) {
        err = "'--' is only valid with run";
        return Parse::Error;
      }
      for (++i; i < args.size(); ++i)
        o.runArgs.emplace_back(args[i]);
      break;
    }

    if (a.starts_with('-')) {
      const OptionSpec *opt = findOption(a);
      if (opt == nullptr) {
        err = "unknown option '" + std::string(a) + "'";
        return Parse::Error;
      }
      if (!appliesTo(*opt, cmd->name)) {
        err = "'" + std::string(a) + "' is not valid for '" +
              std::string(cmd->name) + "'";
        return Parse::Error;
      }
      if (std::find(seen.begin(), seen.end(), a) != seen.end()) {
        err = "'" + std::string(a) + "' is given more than once";
        return Parse::Error;
      }
      seen.push_back(a);

      if (opt->value.empty())
        continue;
      if (i + 1 >= args.size() || args[i + 1].starts_with("--")) {
        err = "'" + std::string(a) + "' needs a value";
        return Parse::Error;
      }
      ++i;
      if (!applyValue(o, *opt, args[i], err))
        return Parse::Error;
      continue;
    }

    if (cmd->positional.empty()) {
      err = "'" + std::string(a) + "' is not an argument for '" +
            std::string(cmd->name) + "'";
      return Parse::Error;
    }
    if (positionals == 1) {
      err = "too many arguments for '" + std::string(cmd->name) +
            "'; expected " + std::string(cmd->positional);
      return Parse::Error;
    }
    o.updateName = std::string(a);
    ++positionals;
  }

  return Parse::Run;
}

void row(std::ostream &out, std::string left, std::string_view right) {
  if (left.size() < 26)
    left.append(26 - left.size(), ' ');
  else
    left += ' ';
  out << "  " << left << right << "\n";
}

void printUsage(std::ostream &out, const CommandSpec *only) {
  if (only == nullptr) {
    out << "usage: smelt <command> [options]\n"
           "       smelt --version | --help\n\n"
           "commands:\n";
    for (const auto &c : kCommands) {
      std::string left(c.name);
      if (!c.positional.empty())
        left += " " + std::string(c.positional);
      row(out, left, c.summary);
    }
    out << "\noptions (the commands each applies to are in brackets):\n";
    for (const auto &o : kOptions) {
      std::string left(o.name);
      if (!o.value.empty())
        left += " " + std::string(o.value);
      row(out, left,
          std::string(o.help) + " [" + std::string(o.commands) + "]");
    }
    return;
  }

  out << "usage: smelt " << only->name;
  if (!only->positional.empty())
    out << " " << only->positional;
  out << " [options]";
  if (only->passthrough)
    out << " [-- ARGS...]";
  out << "\n\n" << only->summary << "\n\noptions:\n";

  bool any = false;
  for (const auto &o : kOptions) {
    if (!appliesTo(o, only->name))
      continue;
    any = true;
    std::string left(o.name);
    if (!o.value.empty())
      left += " " + std::string(o.value);
    row(out, left, o.help);
  }
  if (!any)
    out << "  (none)\n";
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
  const std::string kind = t.type == smelt::TargetType::Exe          ? "exe"
                           : t.type == smelt::TargetType::HeaderOnly ? "hdr"
                                                                     : "lib";
  std::cout << "  " << kind << "  " << fs::relative(out, root).string() << "\n";
}

std::vector<std::string> toolsFor(const Options &o, bool hasRemoteDeps,
                                  bool hasSystem) {
  std::vector<std::string> tools;
  if (o.command == "build" || o.command == "run" || o.command == "install") {
    tools = {"clang", "clang++", "ar"};
  }
  if (o.command == "fetch" || o.command == "update" || hasRemoteDeps) {
    tools.push_back("git");
  }
  if (hasSystem) {
    tools.push_back("pkg-config");
  }
  return tools;
}
} // namespace

int main(int argc, char **argv) {
  ::setenv("GIT_TERMINAL_PROMPT", "0", 1);

  Options opts;
  std::string err;
  switch (parseArgs(argc, argv, opts, err)) {
  case Parse::Help:
    printUsage(std::cout, findCommand(opts.command));
    return 0;
  case Parse::Version:
    std::cout << "smelt " << SMELT_VERSION << "\n";
    return 0;
  case Parse::Error:
    std::cerr << "smelt: " << err << "\nrun 'smelt --help' for usage\n";
    return 2;
  case Parse::Run:
    break;
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
    if (!smelt::requireTools(toolsFor(opts, false, false)))
      return 2;
    return opts.command == "install" ? smelt::installSelf(root, prefix)
                                     : smelt::uninstallSelf(prefix);
  }

  if (opts.command == "tree")
    return smelt::printTree(root);

  if (opts.command == "check") {
    smelt::Diagnostics diags("project.smelt");
    const auto model = smelt::loadModel(root / projectFile, diags);
    if (!model) {
      printDiagnostics(diags);
      return 1;
    }
    std::cout << "ok: project '" << model->project.name << "'\n";
    return 0;
  }

  if (opts.command == "init") {
    return smelt::initProject(root, root.filename().string(), std::cin,
                              std::cout);
  }

  if (opts.command == "fetch" || opts.command == "update") {
    if (!smelt::requireTools(toolsFor(opts, true, false)))
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

  bool rootHasSystem = false;
  {
    smelt::Diagnostics pre("project.smelt");
    if (const auto m = smelt::loadModel(root / projectFile, pre)) {
      for (const auto &dep : m->dependencies) {
        if (!dep.pkgConfig.empty())
          rootHasSystem = true;
      }
    }
  }
  if (rootHasSystem && !smelt::requireTools({"pkg-config"}))
    return 2;

  smelt::Diagnostics diags("project.smelt");
  auto ws = smelt::Workspace::load(root, diags);
  if (!ws) {
    printDiagnostics(diags);
    return 1;
  }
  bool hasRemote = false;
  bool hasSystem = false;
  for (const auto &pkg : ws->packages) {
    for (const auto &dep : pkg.model.dependencies) {
      if (dep.git || dep.registry)
        hasRemote = true;
      if (!dep.pkgConfig.empty())
        hasSystem = true;
    }
  }
  if (!smelt::requireTools(toolsFor(opts, hasRemote, hasSystem)))
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

    std::vector<smelt::NodeKey> keys;
    for (const auto &name : names)
      keys.push_back(smelt::NodeKey{0, name});
    const auto outs = builder.buildSet(keys, opts.jobs);
    if (!outs)
      return 1;
    for (const auto &name : names)
      printOutput(*smelt::findTarget(ws->packages[0], name),
                  outs->at(smelt::NodeKey{0, name}), root);

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

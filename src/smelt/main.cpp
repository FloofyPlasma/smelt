#include "build.hpp"
#include "diagnostics.hpp"
#include "discover.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "process.hpp"
#include "sema.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
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
               "  smelt clean\n";
}

auto parseOptions(int argc, char **argv, Options &o) -> bool {
  if (argc < 2)
    return false;
  o.command = argv[1];
  if (o.command != "build" && o.command != "run" && o.command != "clean")
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
} // namespace

int main(int argc, char **argv) {
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

  std::ifstream in(root / projectFile, std::ios::binary);
  if (!in) {
    std::cerr << "smelt: no " << projectFile << " in " << root.string() << "\n";
    return 2;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string source = buffer.str();

  smelt::Diagnostics diags(projectFile);
  smelt::Lexer lexer(source, diags);
  const auto tokens = lexer.tokenize();
  const auto file = smelt::parseProject(tokens, diags);

  std::optional<smelt::Model> model;
  if (!diags.hasErrors())
    model = smelt::analyze(file, diags);
  if (diags.hasErrors() || !model) {
    printDiagnostics(diags);
    return 1;
  }

  std::optional<smelt::ProfileInfo> profile;
  for (const auto &p : model->profiles) {
    if (p.name == opts.profile)
      profile = p;
  }
  if (!profile) {
    std::cerr << "smelt: unknown profile '" << opts.profile << "'\n";
    return 2;
  }

  std::vector<std::string> warnings;
  const auto sources = smelt::discoverSources(*model, root, diags, warnings);
  for (const auto &w : warnings)
    std::cerr << projectFile << ": warning: " << w << "\n";
  if (!sources || diags.hasErrors()) {
    printDiagnostics(diags);
    return 1;
  }

  smelt::Builder builder(*model, *sources, root, *profile);

  if (opts.command == "build") {
    if (opts.target) {
      if (!builder.build(*opts.target))
        return 1;
    } else {
      for (const auto &t : model->targets) {
        if (!builder.build(t.name))
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
    const auto exe = std::find_if(model->targets.begin(), model->targets.end(),
                                  [](const smelt::TargetInfo &t) {
                                    return t.type == smelt::TargetType::Exe;
                                  });
    if (exe == model->targets.end()) {
      std::cerr << "smelt: no executable target in project\n";
      return 1;
    }
    name = exe->name;
  }

  const smelt::TargetInfo *selected = nullptr;
  for (const auto &t : model->targets) {
    if (t.name == name)
      selected = &t;
  }
  if (!selected) {
    std::cerr << "smelt: unknown target '" << name << "'\n";
    return 1;
  }
  if (selected->type != smelt::TargetType::Exe) {
    std::cerr << "smelt: target '" << name << "' is not an executable\n";
    return 1;
  }

  const auto exePath = builder.build(name);
  if (!exePath)
    return 1;

  std::vector<std::string> runArgv{exePath->string()};
  runArgv.insert(runArgv.end(), opts.runArgs.begin(), opts.runArgs.end());

  const int code = smelt::runProcess(runArgv);
  return code < 0 ? 1 : code;
}

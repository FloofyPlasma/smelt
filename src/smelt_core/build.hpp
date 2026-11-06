#pragma once

#include "console.hpp"
#include "discover.hpp"
#include "module_scan.hpp"
#include "sema.hpp"
#include "workspace.hpp"
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace smelt {
struct BuildDefine {
  std::string name;
  std::string value;
  bool hasValue = false;
  std::string source;
  int rank = 0;
};

class Builder {
public:
  Builder(const Workspace &ws, const std::vector<SourceMap> &sources,
          ProfileInfo profile);
  [[nodiscard]] auto buildSet(const std::vector<NodeKey> &roots,
                              std::size_t jobs)
      -> std::optional<std::map<NodeKey, std::filesystem::path>>;
  [[nodiscard]] auto build(const NodeKey &node, std::size_t jobs = 1)
      -> std::optional<std::filesystem::path>;

  struct CompileUnit {
    std::filesystem::path directory;
    std::filesystem::path file;
    std::filesystem::path object;
    std::vector<std::string> arguments;
  };

  [[nodiscard]] auto compileUnits(const std::vector<std::string> &rootTargets)
      -> std::optional<std::vector<CompileUnit>>;

private:
  struct Usage {
    std::vector<std::string> includes;
    std::vector<std::string> systemIncludes;
    std::vector<BuildDefine> defines;
    std::vector<std::string> options;
    std::vector<std::string> warnings;
    std::vector<std::string> link;
  };

  struct ModuleInfo {
    std::vector<ScanRule> scans;
    std::map<std::string, std::size_t> providerOf;
  };
  auto scanNode(const NodeKey &node) const -> std::optional<ModuleInfo>;

  struct Provider {
    NodeKey owner;
    std::filesystem::path interfacePath;
  };
  std::map<std::string, Provider> providers_;
  std::map<std::string, ScanRule> rules_;
  bool scanned_ = false;

  [[nodiscard]] auto scanWorkspace(const std::vector<NodeKey> &roots) -> bool;

  [[nodiscard]] auto target(const NodeKey &n) const -> const TargetInfo &;
  [[nodiscard]] auto depsOf(const NodeKey &n) const -> std::vector<NodeKey>;
  [[nodiscard]] auto outDir(const NodeKey &n) const -> std::filesystem::path;
  void addPublic(const NodeKey &n, Usage &u) const;
  void walkDeps(const NodeKey &n, std::set<NodeKey> &seen, Usage &u) const;
  [[nodiscard]] auto requirementsOf(const NodeKey &n) const -> Usage;
  void staticLibs(const NodeKey &n, std::set<NodeKey> &seen,
                  std::vector<NodeKey> &post) const;
  [[nodiscard]] auto reachable(const std::vector<NodeKey> &roots) const
      -> std::vector<NodeKey>;
  [[nodiscard]] auto resolveDefines(const Usage &u) const
      -> std::optional<std::vector<std::string>>;
  [[nodiscard]] auto
  compileArgs(const Package &pkg, const SourceFile &src, const Usage &u,
              const std::filesystem::path &obj,
              const std::filesystem::path &dep,
              const std::vector<std::string> &moduleArgs) const
      -> std::vector<std::string>;

  [[nodiscard]] auto prepareStd() -> bool;
  [[nodiscard]] auto stdPcm() const -> std::filesystem::path;
  [[nodiscard]] auto closureOf(const NodeKey &node) const -> std::set<NodeKey>;
  [[nodiscard]] auto displayName(const NodeKey &n) const -> std::string;
  [[nodiscard]] auto resolveImports(const NodeKey &node, const ScanRule &rule,
                                    std::vector<std::string> &flags,
                                    std::vector<std::filesystem::path> &inputs,
                                    std::string &err) const -> bool;

  bool stdEnabled_ = false;
  std::optional<std::filesystem::path> stdSource_;
  const Workspace &ws_;
  const std::vector<SourceMap> &sources_;
  ProfileInfo profile_;
  Console console_;
  std::map<NodeKey, std::filesystem::path> outputs_;
};
} // namespace smelt

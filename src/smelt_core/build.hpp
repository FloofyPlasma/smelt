#pragma once

#include "discover.hpp"
#include "sema.hpp"
#include "workspace.hpp"
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace smelt {
class Builder {
public:
  Builder(const Workspace &ws, const std::vector<SourceMap> &sources,
          ProfileInfo profile);

  [[nodiscard]] auto build(const NodeKey &node)
      -> std::optional<std::filesystem::path>;

  struct Define {
    std::string name;
    std::string value;
    bool hasValue = false;
    std::string source;
    int rank = 0;
  };

private:
  struct Usage {
    std::vector<std::string> includes;
    std::vector<Define> defines;
    std::vector<std::string> options;
    std::vector<std::string> link;
  };

  [[nodiscard]] auto target(const NodeKey &n) const -> const TargetInfo &;
  [[nodiscard]] auto depsOf(const NodeKey &n) const -> std::vector<NodeKey>;
  [[nodiscard]] auto outDir(const NodeKey &n) const -> std::filesystem::path;
  void addPublic(const NodeKey &n, Usage &u) const;
  void walkDeps(const NodeKey &n, std::set<NodeKey> &seen, Usage &u) const;
  [[nodiscard]] auto requirementsOf(const NodeKey &n) const -> Usage;
  void staticLibs(const NodeKey &n, std::set<NodeKey> &seen,
                  std::vector<NodeKey> &post) const;

  [[nodiscard]] auto compileArgs(const Package &pkg, const SourceFile &src,
                                 const Usage &u,
                                 const std::filesystem::path &obj,
                                 const std::filesystem::path &dep) const
      -> std::vector<std::string>;
  [[nodiscard]] auto resolveDefines(const Usage &u) const
      -> std::optional<std::vector<std::string>>;

  [[nodiscard]] auto compileOne(const Package &pkg, const SourceFile &src,
                                const Usage &u,
                                const std::filesystem::path &objDir)
      -> std::optional<std::filesystem::path>;
  [[nodiscard]] auto archive(const std::string &name,
                             const std::filesystem::path &dir,
                             const std::vector<std::filesystem::path> &objects)
      -> std::optional<std::filesystem::path>;
  [[nodiscard]] auto link(const std::string &name,
                          const std::filesystem::path &dir,
                          const std::vector<std::filesystem::path> &objects,
                          const std::vector<std::filesystem::path> &archives,
                          const std::vector<std::string> &flags)
      -> std::optional<std::filesystem::path>;

  const Workspace &ws_;
  const std::vector<SourceMap> &sources_;
  ProfileInfo profile_;
  std::map<NodeKey, std::filesystem::path> outputs_;
  std::set<NodeKey> active_;
};
} // namespace smelt

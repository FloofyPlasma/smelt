#pragma once

#include "discover.hpp"
#include "sema.hpp"
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>
namespace smelt {
class Builder {
public:
  Builder(const Model &model, const SourceMap &sources,
          std::filesystem::path projectRoot, ProfileInfo profile);

  [[nodiscard]] auto build(const std::string &name)
      -> std::optional<std::filesystem::path>;

private:
  [[nodiscard]] auto find(const std::string &name) const -> const TargetInfo *;
  [[nodiscard]] auto targetDir(const std::string &name) const
      -> std::filesystem::path;

  [[nodiscard]] auto includeDirs(const TargetInfo &t,
                                 const TargetSources &src) const
      -> std::vector<std::string>;
  void collectPublicIncludes(const std::string &name,
                             std::set<std::string> &visited,
                             std::vector<std::string> &out) const;
  void collectLibs(const std::string &name, std::set<std::string> &seen,
                   std::vector<std::string> &post) const;

  [[nodiscard]] auto stdFlag(bool cxx) const -> std::string;
  [[nodiscard]] auto compileArgs(const TargetInfo &t, const SourceFile &src,
                                 const std::vector<std::string> &includes,
                                 const std::filesystem::path &obj,
                                 const std::filesystem::path &dep) const
      -> std::vector<std::string>;

  [[nodiscard]] auto compileOne(const TargetInfo &t, const SourceFile &src,
                                const std::vector<std::string> &includes,
                                const std::filesystem::path &objDir) const
      -> std::optional<std::filesystem::path>;
  [[nodiscard]] auto archive(const TargetInfo &t,
                             const std::vector<std::filesystem::path> &obects)
      -> std::optional<std::filesystem::path>;
  [[nodiscard]] auto link(const TargetInfo &t,
                          const std::vector<std::filesystem::path> &objects,
                          const std::vector<std::filesystem::path> &archives,
                          const std::vector<std::string> &flags)
      -> std::optional<std::filesystem::path>;

  const Model &model_;
  const SourceMap &sources_;
  std::filesystem::path root_;
  ProfileInfo profile_;
  std::map<std::string, std::filesystem::path> outputs_;
};
} // namespace smelt

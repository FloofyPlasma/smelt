#include "discover.hpp"
#include "glob.hpp"
#include "sema.hpp"
#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace fs = std::filesystem;

namespace smelt {
namespace {
constexpr std::string_view kPlatform = "linux";

auto isSourceFile(const fs::path &p) -> bool {
  const fs::path ext = p.extension();
  return ext == ".c" || ext == ".cpp";
}

auto matchesAny(const std::vector<std::string> &patterns,
                const std::string &path) -> bool {
  return std::any_of(patterns.begin(), patterns.end(),
                     [&](const std::string &p) { return globMatch(p, path); });
}

auto platformRules(const TargetInfo &t) -> const PlatformRules * {
  const auto it = t.platforms.find(std::string(kPlatform));
  return it == t.platforms.end() ? nullptr : &it->second;
}
} // namespace

auto discoverSources(const Model &model,
                     const std::filesystem::path &projectRoot,
                     Diagnostics &diags, std::vector<std::string> &warnings)
    -> std::optional<SourceMap> {
  SourceMap out;
  std::map<std::string, std::string> owners;
  bool ok = true;

  for (const auto &t : model.targets) {
    const std::string rootRel = t.sourceDir.value_or("src/" + t.name);
    const fs::path root = projectRoot / rootRel;

    if (!fs::is_directory(root)) {
      if (t.sourceDir) {
        diags.error(t.loc, "source_dir '" + rootRel + "' does not exist");
      } else {
        diags.error(t.loc, "target '" + t.name +
                               "' has no source directory; "
                               "expected '" +
                               rootRel + "' (or set source_dir)");
      }
      ok = false;
      continue;
    }

    for (const auto *list : {&t.publicIncludes, &t.privateIncludes}) {
      for (const auto &dir : *list) {
        if (!fs::is_directory(projectRoot / dir)) {
          diags.error(t.loc, "include directory '" + dir + "' does not exist");
          ok = false;
        }
      }
    }

    std::vector<std::string> everything;
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
      if (entry.is_regular_file())
        everything.push_back(fs::relative(entry.path(), root).generic_string());
    }

    const PlatformRules *plat = platformRules(t);

    auto warnUnmatched = [&](const std::vector<std::string> &patterns,
                             const std::string &label) {
      for (const auto &p : patterns) {
        const bool used =
            std::any_of(everything.begin(), everything.end(),
                        [&](const std::string &f) { return globMatch(p, f); });
        if (!used) {
          warnings.push_back("target '" + t.name + "': " + label +
                             " pattern '" + p + "' matches no file");
        }
      }
    };
    warnUnmatched(t.files, "files");
    warnUnmatched(t.exclude, "exclude");
    if (plat) {
      const std::string prefix = "on " + std::string(kPlatform) + " ";
      warnUnmatched(plat->files, prefix + "files");
      warnUnmatched(plat->exclude, prefix + "exclude");
    }

    std::vector<std::string> allow = t.files;
    std::vector<std::string> exclude = t.exclude;
    if (plat) {
      allow.insert(allow.end(), plat->files.begin(), plat->files.end());
      exclude.insert(exclude.end(), plat->exclude.begin(), plat->exclude.end());
    }
    const bool restricted = !allow.empty();

    std::vector<std::pair<std::string, fs::path>> candidates;
    for (const auto &rel : everything) {
      const fs::path abs = root / rel;
      if (!isSourceFile(abs))
        continue;
      if (restricted && !matchesAny(allow, rel))
        continue;
      if (matchesAny(exclude, rel))
        continue;
      const fs::path canon = fs::canonical(abs);
      candidates.emplace_back(canon.string(), canon);
    }

    if (candidates.empty()) {
      diags.error(t.loc,
                  "target '" + t.name + "' has no sources after filtering");
      ok = false;
      continue;
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    TargetSources ts;
    ts.root = fs::canonical(root);

    for (const auto &[key, canon] : candidates) {
      const std::string relProject =
          fs::relative(canon, projectRoot).generic_string();

      if (relProject.rfind("..", 0) == 0) {
        diags.error(t.loc,
                    "source file '" + relProject + "'lies outside the project");
        ok = false;
        continue;
      }

      if (const auto it = owners.find(key); it != owners.end()) {
        diags.error(t.loc, "source file '" + relProject +
                               "' belongs to both targets '" + it->second +
                               "' and '" + t.name + "'");
        ok = false;
        continue;
      }
      owners[key] = t.name;

      ts.files.push_back({relProject, canon, canon.extension() == ".cpp"});
    }

    out[t.name] = std::move(ts);
  }

  if (!ok)
    return std::nullopt;
  return out;
}

} // namespace smelt

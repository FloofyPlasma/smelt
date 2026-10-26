#include "registry.hpp"
#include "ast.hpp"
#include "diagnostics.hpp"
#include "git.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "process.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace smelt {
namespace {
struct UpstreamSpec {
  std::string url;
  std::string commit;
};

auto readFile(const fs::path &p, std::string &out) -> bool {
  std::ifstream in(p, std::ios::binary);
  if (!in)
    return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

auto readSourceFile(const fs::path &file, Diagnostics &out)
    -> std::optional<UpstreamSpec> {
  Diagnostics diags(file.string());

  std::string text;
  if (!readFile(file, text)) {
    diags.error({}, "cannot read " + file.string());
  } else {
    Lexer lexer(text, diags);
    const auto tokens = lexer.tokenize();
    const ProjectFile ast = parseSourceFile(tokens, diags);

    if (!diags.hasErrors() && ast.sources.size() != 1)
      diags.error({}, "source.smelt must contain exactly one 'source' block");

    if (!diags.hasErrors()) {
      UpstreamSpec spec;
      for (const auto &a : ast.sources.front().assigns) {
        const bool isString = a.value.kind == Value::Kind::String;
        if (a.key == "git" && isString)
          spec.url = a.value.text;
        else if (a.key == "commit" && isString)
          spec.commit = a.value.text;
        else if (a.key == "tag")
          diags.error(a.loc, "registry entries pin a 'commit'; write the "
                             "upstream tag in a '//' comment instead");
        else
          diags.error(a.loc,
                      "setting '" + a.key + "' is not valid in source.smelt");
      }
      if (spec.url.empty())
        diags.error({}, "source needs a 'git'");
      else if (!isAllowedGitUrl(spec.url))
        diags.error({}, "'git' must be an https:// URL");
      if (spec.commit.empty())
        diags.error({}, "source needs a 'commit'");
      else if (!isFullCommitHash(spec.commit))
        diags.error({}, "'commit' must be a full 40-character hash");

      if (!diags.hasErrors())
        return spec;
    }
  }

  for (const auto &d : diags.items())
    out.add(d);
  return std::nullopt;
}

auto registryUrl(Diagnostics &diags) -> std::optional<std::string> {
  const char *env = std::getenv("SMELT_REGISTRY");
  if (env == nullptr || *env == '\0') {
    diags.error({}, "no registry configured; set SMELT_REGISTRY to the "
                    "registry repository URL");
    return std::nullopt;
  }
  const std::string url = env;
  if (!isAllowedGitUrl(url)) {
    diags.error({}, "SMELT_REGISTRY must be an https:// URL");
    return std::nullopt;
  }
  return url;
}
} // namespace

auto registryDir(const fs::path &root) -> fs::path {
  return root / ".smelt" / "registry";
}

auto syncRegistry(const fs::path &root, const std::string &commit,
                  Diagnostics &diags) -> std::optional<std::string> {
  const auto url = registryUrl(diags);
  if (!url)
    return std::nullopt;

  const fs::path dir = registryDir(root);
  if (!ensureClone(*url, dir)) {
    diags.error({}, "cannot clone registry " + *url);
    return std::nullopt;
  }

  std::string target = commit;
  if (target.empty()) {
    if (runProcess({"git", "-C", dir.string(), "fetch", "-q", "origin"}) != 0) {
      diags.error({}, "cannot update registry " + *url);
      return std::nullopt;
    }
    const auto head = resolveTag(dir, "origin/HEAD");
    if (!head) {
      diags.error({}, "registry has no default branch");
      return std::nullopt;
    }
    target = *head;
  }

  if (!checkoutCommit(dir, target)) {
    diags.error({}, "cannot check out registry commit " + target);
    return std::nullopt;
  }
  return target;
}

auto readRegistryEntry(const fs::path &regDir, const std::string &name,
                       const std::string &version, Diagnostics &diags)
    -> std::optional<RegistryEntry> {
  const fs::path dir = regDir / "packages" / name / version;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    diags.error({}, "registry has no package '" + name + "' at version '" +
                        version + "'");
    return std::nullopt;
  }

  const auto spec = readSourceFile(dir / "source.smelt", diags);
  if (!spec)
    return std::nullopt;

  RegistryEntry e;
  e.upstreamUrl = spec->url;
  e.upstreamCommit = spec->commit;
  e.overlayFile = dir / "overlay.smelt";
  if (!fs::exists(e.overlayFile, ec)) {
    diags.error({}, "registry package '" + name + "' has no overlay.smelt");
    return std::nullopt;
  }
  return e;
}

} // namespace smelt

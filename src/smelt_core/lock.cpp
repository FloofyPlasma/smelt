#include "lock.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace smelt {
auto readLock(const std::filesystem::path &file, Lock &out, std::string &err)
    -> bool {
  out = Lock{};
  std::error_code ec;
  if (!fs::exists(file, ec))
    return true;

  std::ifstream in(file);
  if (!in) {
    err = "cannot read " + file.string();
    return false;
  }

  std::string line;
  int lineNo = 0;
  auto fail = [&](const std::string &msg) -> bool {
    err = file.string() + ":" + std::to_string(lineNo) + ": " + msg;
    return false;
  };

  while (std::getline(in, line)) {
    ++lineNo;
    if (line.empty() || line[0] == '#')
      continue;

    std::istringstream ss(line);
    std::string kind;
    ss >> kind;

    if (kind == "registry-commit") {
      std::string hash;
      if (!(ss >> hash) || !out.registryCommit.empty())
        return fail("bad registry-commit line");
      out.registryCommit = hash;
      continue;
    }

    std::string name;
    LockEntry e;
    e.kind = kind;
    if (!(ss >> name >> e.url >> e.refKind >> e.ref >> e.commit))
      return fail("malformed lock entry");

    const bool gitOk =
        kind == "git" && (e.refKind == "tag" || e.refKind == "commit");
    const bool registryOk = kind == "registry" && e.refKind == "version";
    if (!gitOk && !registryOk)
      return fail("unknown entry kind or ref kind");
    if (out.entries.count(name))
      return fail("duplicate entry for '" + name + "'");
    out.entries[name] = std::move(e);
  }
  return true;
}

auto writeLock(const fs::path &file, const Lock &lock) -> bool {
  const fs::path tmp = fs::path(file.string() + ".tmp");
  {
    std::ofstream out(tmp);
    if (!out)
      return false;
    out << "# smelt.lock; Generated file. Please do not edit by hand. It is "
           "recommended to commit this file.\n";
    if (!lock.registryCommit.empty())
      out << "registry-commit " << lock.registryCommit << "\n";
    for (const auto &[name, e] : lock.entries) {
      out << e.kind << " " << name << " " << e.url << " " << e.refKind << " "
          << e.ref << " " << e.commit << "\n";
    }
    if (!out)
      return false;
  }

  std::error_code ec;
  fs::rename(tmp, file, ec);
  return !ec;
}
} // namespace smelt

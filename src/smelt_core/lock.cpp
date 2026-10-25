#include "lock.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace smelt {
auto readLock(const std::filesystem::path &file,
              std::map<std::string, LockEntry> &out, std::string &err) -> bool {
  out.clear();
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
  while (std::getline(in, line)) {
    ++lineNo;
    if (line.empty() || line[0] == '#')
      continue;

    std::istringstream ss(line);
    std::string kind;
    std::string name;
    LockEntry e;
    const bool parsed = static_cast<bool>(ss >> kind >> name >> e.url >>
                                          e.refKind >> e.ref >> e.commit);
    if (!parsed || kind != "git" ||
        (e.refKind != "tag" && e.refKind != "commit")) {
      err = file.string() + ":" + std::to_string(lineNo) +
            ": malformed lock entry";
      return false;
    }
    if (out.count(name)) {
      err = file.string() + ":" + std::to_string(lineNo) +
            ": duplicate entry for '" + name + "'";
      return false;
    }
    out[name] = std::move(e);
  }
  return true;
}

auto writeLock(const fs::path &file,
               const std::map<std::string, LockEntry> &entries) -> bool {
  const fs::path tmp = fs::path(file.string() + ".tmp");
  {
    std::ofstream out(tmp);
    if (!out)
      return false;
    out << "# smelt.lock; Generated file. Please do not edit by hand. It is "
           "recommended to commit this file.\n";
    for (const auto &[name, e] : entries) {
      out << "git " << name << " " << e.url << " " << e.refKind << " " << e.ref
          << " " << e.commit << "\n";
    }
    if (!out)
      return false;
  }

  std::error_code ec;
  fs::rename(tmp, file, ec);
  return !ec;
}
} // namespace smelt

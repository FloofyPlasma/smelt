#include "compdb.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace smelt {
namespace {
auto jsonEscape(const std::string &s) -> std::string {
  std::string out;
  out.reserve(s.size() + 2);
  for (const char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\t':
      out += "\\t";
      break;
    case '\r':
      out += "\\r";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        static const char hex[] = "0123456789abcdef";
        out += "\\u00";
        out += hex[(c >> 4) & 0xF];
        out += hex[c & 0xF];
      } else {
        out += c;
      }
      break;
    }
  }
  return out;
}
} // namespace

auto writeCompileDatabase(const fs::path &file,
                          const std::vector<Builder::CompileUnit> &units)
    -> bool {
  const fs::path tmp = fs::path(file.string() + ".tmp");
  {
    std::ofstream out(tmp);
    if (!out)
      return false;

    out << "[\n";
    for (std::size_t i = 0; i < units.size(); ++i) {
      const auto &u = units[i];
      out << "  {\n";
      out << "    \"directory\": \"" << jsonEscape(u.directory.string())
          << "\",\n";
      out << "    \"file\": \"" << jsonEscape(u.file.string()) << "\",\n";
      out << "    \"output\": \"" << jsonEscape(u.object.string()) << "\",\n";
      out << "    \"arguments\": [";
      for (std::size_t a = 0; a < u.arguments.size(); ++a) {
        if (a > 0)
          out << ", ";
        out << "\"" << jsonEscape(u.arguments[a]) << "\"";
      }
      out << "]\n";
      out << "  }" << (i + 1 < units.size() ? "," : "") << "\n";
    }
    out << "]\n";
    if (!out)
      return false;
  }

  std::error_code ec;
  fs::rename(tmp, file, ec);
  return !ec;
}

} // namespace smelt

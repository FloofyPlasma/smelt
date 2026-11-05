#include "module_scan.hpp"
#include "process.hpp"
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace smelt {
namespace {
struct Json {
  enum class Kind { Null, Bool, Number, String, Array, Object };
  Kind kind = Kind::Null;
  std::string text;
  std::vector<Json> items;
  std::vector<std::string> keys;
  std::vector<Json> values;

  auto field(const std::string &key) const -> const Json * {
    for (std::size_t i = 0; i < keys.size(); ++i) {
      if (keys[i] == key)
        return &values[i];
    }
    return nullptr;
  }
};

class JsonReader {
public:
  explicit JsonReader(const std::string &s) : s_(s) {}

  auto parse(Json &out) -> bool {
    if (!value(out))
      return false;
    skip();
    return pos_ == s_.size();
  }

private:
  const std::string &s_;
  std::size_t pos_ = 0;

  void skip() {
    while (pos_ < s_.size() &&
           std::isspace(static_cast<unsigned char>(s_[pos_])))
      ++pos_;
  }

  auto eat(char c) -> bool {
    skip();
    if (pos_ < s_.size() && s_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  auto literal(std::string_view word) -> bool {
    if (s_.compare(pos_, word.size(), word) != 0)
      return false;
    pos_ += word.size();
    return true;
  }

  auto string(std::string &out) -> bool {
    if (!eat('"'))
      return false;
    out.clear();
    while (pos_ < s_.size() && s_[pos_] != '"') {
      const char c = s_[pos_++];
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= s_.size())
        return false;
      const char e = s_[pos_++];
      switch (e) {
      case 'n':
        out.push_back('\n');
        break;
      case 't':
        out.push_back('\t');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 'u':
        if (pos_ + 4 > s_.size())
          return false;
        pos_ += 4;
        out.push_back('?');
        break;
      default:
        out.push_back(e);
        break; // \" \\ \/
      }
    }
    if (pos_ >= s_.size())
      return false;
    ++pos_; // closing quote
    return true;
  }

  auto value(Json &out) -> bool {
    skip();
    if (pos_ >= s_.size())
      return false;
    const char c = s_[pos_];
    if (c == '{')
      return object(out);
    if (c == '[')
      return array(out);
    if (c == '"') {
      out.kind = Json::Kind::String;
      return string(out.text);
    }
    if (c == 't') {
      out.kind = Json::Kind::Bool;
      return literal("true");
    }
    if (c == 'f') {
      out.kind = Json::Kind::Bool;
      return literal("false");
    }
    if (c == 'n') {
      out.kind = Json::Kind::Null;
      return literal("null");
    }
    out.kind = Json::Kind::Number;
    const std::size_t start = pos_;
    while (pos_ < s_.size() &&
           (std::isdigit(static_cast<unsigned char>(s_[pos_])) ||
            std::string_view("+-.eE").find(s_[pos_]) != std::string_view::npos))
      ++pos_;
    return pos_ > start;
  }

  auto object(Json &out) -> bool {
    out.kind = Json::Kind::Object;
    eat('{');
    if (eat('}'))
      return true;
    while (true) {
      std::string key;
      if (!string(key) || !eat(':'))
        return false;
      Json v;
      if (!value(v))
        return false;
      out.keys.push_back(std::move(key));
      out.values.push_back(std::move(v));
      if (eat(','))
        continue;
      return eat('}');
    }
  }

  auto array(Json &out) -> bool {
    out.kind = Json::Kind::Array;
    eat('[');
    if (eat(']'))
      return true;
    while (true) {
      Json v;
      if (!value(v))
        return false;
      out.items.push_back(std::move(v));
      if (eat(','))
        continue;
      return eat(']');
    }
  }
};

auto jsonEscape(const std::string &s) -> std::string {
  std::string out;
  for (const char c : s) {
    if (c == '"' || c == '\\')
      out += '\\';
    out += c;
  }
  return out;
}

auto writeDatabase(const fs::path &file, const std::vector<ScanUnit> &units)
    -> bool {
  fs::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary);
  if (!out)
    return false;
  out << "[\n";
  for (std::size_t i = 0; i < units.size(); ++i) {
    const ScanUnit &u = units[i];
    out << "  {\n    \"directory\": \"" << jsonEscape(u.directory.string())
        << "\",\n    \"file\": \"" << jsonEscape(u.file.string())
        << "\",\n    \"output\": \"" << jsonEscape(u.output.string())
        << "\",\n    \"arguments\": [";
    for (std::size_t a = 0; a < u.arguments.size(); ++a) {
      if (a > 0)
        out << ", ";
      out << "\"" << jsonEscape(u.arguments[a]) << "\"";
    }
    out << "]\n  }" << (i + 1 < units.size() ? "," : "") << "\n";
  }
  out << "]\n";
  return static_cast<bool>(out);
}

auto readModules(const Json *list, std::vector<ModuleName> &out) -> bool {
  if (list == nullptr)
    return true;
  if (list->kind != Json::Kind::Array)
    return false;
  for (const Json &item : list->items) {
    const Json *name = item.field("logical-name");
    if (item.kind != Json::Kind::Object || name == nullptr ||
        name->kind != Json::Kind::String)
      return false;
    ModuleName m;
    m.name = name->text;
    if (const Json *src = item.field("source-path");
        src != nullptr && src->kind == Json::Kind::String)
      m.sourcePath = src->text;
    out.push_back(std::move(m));
  }
  return true;
}
} // namespace

auto mayUseModules(const fs::path &file) -> bool {
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos)
      continue;
    const std::string_view s(line.c_str() + first, line.size() - first);
    if (s.rfind("import ", 0) == 0 || s.rfind("module ", 0) == 0 ||
        s.rfind("export module", 0) == 0 || s.rfind("export import", 0) == 0)
      return true;
  }
  return false;
}

auto scanModules(const std::vector<ScanUnit> &units, const fs::path &dbFile,
                 std::map<std::string, ScanRule> &byOutput, std::string &err)
    -> bool {
  byOutput.clear();
  if (!writeDatabase(dbFile, units)) {
    err = "cannot write " + dbFile.string();
    return false;
  }

  const auto text = runCapture({"clang-scan-deps", "-format=p1689",
                                "-compilation-database", dbFile.string()},
                               false);
  if (!text) {
    err = "clang-scan-deps failed (is it onstalled and on PATH?)";
    return false;
  }

  Json root;
  if (!JsonReader(*text).parse(root) || root.kind != Json::Kind::Object) {
    err = "clang-scan-deps output could not be read";
    return false;
  }
  const Json *rules = root.field("rules");
  if (rules == nullptr || rules->kind != Json::Kind::Array)
    return true;

  for (const Json &rule : rules->items) {
    const Json *output = rule.field("primary-output");
    if (output == nullptr || output->kind != Json::Kind::String) {
      err = "clang-scan-deps rule has no primary-output";
      return false;
    }
    ScanRule r;
    if (!readModules(rule.field("provides"), r.provides) ||
        !readModules(rule.field("requires"), r.needs)) {
      err = "clang-scan-deps output has an unexpected shape";
      return false;
    }
    byOutput[output->text] = std::move(r);
  }
  return true;
}

} // namespace smelt

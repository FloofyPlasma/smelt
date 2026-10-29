#include "init.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <ostream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace smelt {
namespace {
auto trim(const std::string &s) -> std::string {
  const auto b = s.find_first_not_of(" \t\r");
  if (b == std::string::npos)
    return {};
  const auto e = s.find_last_not_of(" \t\r");
  return s.substr(b, e - b + 1);
}

auto validName(const std::string &s) -> bool {
  if (s.empty())
    return false;
  for (const char c : s) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok)
      return false;
  }
  return true;
}

auto validVersion(const std::string &s) -> bool {
  if (s.empty() || s.front() == '.' || s.back() == '.')
    return false;
  bool digit = false;
  for (const char c : s) {
    if (std::isdigit(static_cast<unsigned char>(c)))
      digit = true;
    else if (c != '.')
      return false;
  }
  return digit;
}

auto validKind(const std::string &s) -> bool {
  return s == "exe" || s == "static_lib";
}

auto validStd(const std::string &s) -> bool {
  return s == "11" || s == "14" || s == "17" || s == "20" || s == "23" ||
         s == "26";
}

auto ask(std::istream &in, std::ostream &out, const std::string &question,
         const std::string &def,
         const std::function<bool(const std::string &)> &valid,
         const std::string &hint) -> std::string {
  while (true) {
    out << question << " [" << def << "]: " << std::flush;
    std::string line;
    if (!std::getline(in, line)) {
      out << "\n";
      return def;
    }
    const std::string answer = trim(line);
    if (answer.empty())
      return def;
    if (valid(answer))
      return answer;
    out << "  " << hint << "\n";
  }
}

auto askYesNo(std::istream &in, std::ostream &out, const std::string &question)
    -> bool {
  const std::string answer = ask(
      in, out, question + " (y/n)", "y",
      [](const std::string &s) {
        return s == "y" || s == "yes" || s == "n" || s == "no";
      },
      "answer y or n");
  return answer == "y" || answer == "yes";
}

auto identFrom(std::string name) -> std::string {
  for (char &c : name) {
    if (c == '-')
      c = '_';
  }
  return name;
}
} // namespace
auto initProject(const fs::path &dir, const std::string &dirName,
                 std::istream &in, std::ostream &out) -> int {
  std::error_code ec;
  if (fs::exists(dir / "project.smelt", ec)) {
    out << "smelt: project.smelt already exists\n";
    return 1;
  }

  const std::string nameDefault = validName(dirName) ? dirName : "app";
  const std::string name = ask(in, out, "Project name", nameDefault, validName,
                               "use letters, digits, '_' or '-'");
  const std::string version = ask(in, out, "Version", "0.1.0", validVersion,
                                  "use numbers separated by dots, like 0.1.0");
  const std::string kind = ask(in, out, "Kind (exe or static_lib)", "exe",
                               validKind, "answer exe or static_lib");
  const std::string cxxStd = ask(in, out, "C++ standard", "20", validStd,
                                 "answer 11, 14, 17, 20, 23, or 26");

  const bool lib = kind == "static_lib";
  const std::string id = identFrom(name);

  std::string projectText = "project \"" + name + "\" {\n    version = \"" +
                            version + "\";\n    cxx_std = " + cxxStd +
                            ";\n}\n\ntarget \"" + name +
                            "\" {\n    type = " + kind + ";\n";

  if (lib)
    projectText += "    export = true;\n    public_includes = [\"include\"];\n";
  projectText += "}\n";

  std::vector<std::pair<fs::path, std::string>> files{
      {dir / "project.smelt", projectText}};
  if (lib) {
    files.push_back({dir / "include" / (name + ".hpp"),
                     "#pragma once\n\nint " + id + "_answer();\n"});
    files.push_back({dir / "src" / name / (name + ".cpp"),
                     "#include \"" + name + ".hpp\"\n\nint " + id +
                         "_answer() { return 42; }\n"});
  } else {
    files.push_back(
        {dir / "src" / name / "main.cpp", "#include <cstdio>\n\nint main() {\n"
                                          "  std::puts(\"hello from " +
                                              name + "\");\n  return 0;\n}\n"});
  }

  for (const auto &[path, text] : files) {
    if (fs::exists(path, ec)) {
      out << "smelt: " << fs::relative(path, dir).string()
          << " already exists\n";
      return 1;
    }
  }

  out << "\nWill create:\n";
  for (const auto &[path, text] : files)
    out << "  " << fs::relative(path, dir).string() << "\n";
  if (!askYesNo(in, out, "Create these files")) {
    out << "nothing written\n";
    return 1;
  }

  for (const auto &[path, text] : files) {
    fs::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    f << text;
    if (!f) {
      out << "smelt: cannot write " << fs::relative(path, dir).string() << "\n";
      return 1;
    }
    out << "created " << fs::relative(path, dir).string() << "\n";
  }
  return 0;
}
} // namespace smelt

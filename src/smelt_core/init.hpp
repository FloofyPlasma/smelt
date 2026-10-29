#pragma once

#include <filesystem>
#include <istream>
#include <ostream>
#include <string>
namespace smelt {
[[nodiscard]] auto initProject(const std::filesystem::path &dir,
                               const std::string &dirName, std::istream &in,
                               std::ostream &out) -> int;
}

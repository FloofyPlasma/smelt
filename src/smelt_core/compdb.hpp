#pragma once

#include "build.hpp"
#include <filesystem>
#include <vector>
namespace smelt {
[[nodiscard]] auto
writeCompileDatabase(const std::filesystem::path &file,
                     const std::vector<Builder::CompileUnit> &units) -> bool;
}

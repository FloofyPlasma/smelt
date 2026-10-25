#pragma once

#include "diagnostics.hpp"
#include <filesystem>
namespace smelt {
[[nodiscard]] auto fetchDependencies(const std::filesystem::path &root,
                                     Diagnostics &diags) -> bool;
}

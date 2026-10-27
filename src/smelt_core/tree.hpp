#pragma once

#include <filesystem>
namespace smelt {
[[nodiscard]] auto printTree(const std::filesystem::path &root) -> int;
}

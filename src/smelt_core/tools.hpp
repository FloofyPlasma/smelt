#pragma once

#include <optional>
#include <string>
#include <vector>
namespace smelt {
[[nodiscard]] auto findOnPath(const std::string &name)
    -> std::optional<std::string>;

[[nodiscard]] auto requireTools(const std::vector<std::string> &names) -> bool;
} // namespace smelt

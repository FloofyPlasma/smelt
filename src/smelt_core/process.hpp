#pragma once

#include <string>
#include <vector>
namespace smelt {
[[nodiscard]] auto runProcess(const std::vector<std::string> &argv) -> int;

[[nodiscard]] auto joinCommand(const std::vector<std::string> &argv)
    -> std::string;
} // namespace smelt

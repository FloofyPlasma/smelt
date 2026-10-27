#pragma once

#include <optional>
#include <string>
#include <vector>
namespace smelt {
[[nodiscard]] auto runProcess(const std::vector<std::string> &argv) -> int;

[[nodiscard]] auto runProcessLogged(const std::vector<std::string> &argv,
                                    const std::string &logFile) -> int;

[[nodiscard]] auto joinCommand(const std::vector<std::string> &argv)
    -> std::string;

[[nodiscard]] auto runCapture(const std::vector<std::string> &argv)
    -> std::optional<std::string>;
} // namespace smelt

#pragma once

#include <filesystem>
#include <string>
#include <vector>
namespace smelt {
[[nodiscard]] auto hashHex(const std::string &text) -> std::string;

[[nodiscard]] auto signatureOf(const std::vector<std::string> &args)
    -> std::string;

[[nodiscard]] auto fileHash(const std::filesystem::path &p) -> std::string;
} // namespace smelt

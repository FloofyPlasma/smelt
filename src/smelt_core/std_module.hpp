#pragma once

#include <filesystem>
#include <optional>
#include <string>
namespace smelt {

inline constexpr int kMinClangForStdModule = 18;

[[nodiscard]] auto clangMajorVersion() -> std::optional<int>;

[[nodiscard]] auto clangVersionText() -> std::optional<std::string>;

[[nodiscard]] auto findStdModule(std::string &err)
    -> std::optional<std::filesystem::path>;
} // namespace smelt

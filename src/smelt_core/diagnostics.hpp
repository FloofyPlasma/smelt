#pragma once

#include <cstdint>
#include <format>
#include <string>
#include <vector>

namespace smelt {
struct SourceLoc {
  std::uint32_t line = 1;
  std::uint32_t column = 1;
};

struct Diagnostic {
  std::string file;
  SourceLoc loc;
  std::string message;
};

class Diagnostics {
public:
  explicit Diagnostics(std::string file) : file_(std::move(file)) {}

  void error(const SourceLoc loc, std::string message) {
    items_.push_back(
        {.file = file_, .loc = loc, .message = std::move(message)});
  }

  [[nodiscard]] auto hasErrors() const -> bool { return !items_.empty(); }
  [[nodiscard]] auto items() const -> const std::vector<Diagnostic> & {
    return items_;
  }

  [[nodiscard]] static auto format(const Diagnostic &d) -> std::string {
    return std::format("{}:{}:{}: error: {}", d.file, d.loc.line, d.loc.column,
                       d.message);
  }

private:
  std::string file_;
  std::vector<Diagnostic> items_;
};
} // namespace smelt

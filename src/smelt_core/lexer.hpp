#pragma once

#include "diagnostics.hpp"
#include "token.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
namespace smelt {
class Lexer {
public:
  Lexer(std::string_view source, Diagnostics &diags);

  [[nodiscard]] auto tokenize() -> std::vector<Token>;

private:
  [[nodiscard]] auto atEnd() const -> bool { return pos_ >= src_.size(); }
  [[nodiscard]] auto peek(std::size_t ahead = 0) const -> char;
  auto advance() -> char;
  [[nodiscard]] auto here() const -> SourceLoc {
    return {.line = line_, .column = column_};
  }

  void skipTrivia();
  [[nodiscard]] auto lexToken() -> Token;
  [[nodiscard]] auto lexWord(std::size_t start, SourceLoc loc) -> Token;
  [[nodiscard]] auto lexNumber(std::size_t start, SourceLoc loc) -> Token;
  [[nodiscard]] auto lexString(std::size_t start, SourceLoc loc) -> Token;
  [[nodiscard]] auto make(TokenKind kind, std::size_t start,
                          SourceLoc loc) const -> Token;

  std::string_view src_;
  Diagnostics &diags_;
  std::size_t pos_ = 0;
  std::uint32_t line_ = 1;
  std::uint32_t column_ = 1;
};

[[nodiscard]] auto decodeString(std::string_view quoted) -> std::string;
} // namespace smelt

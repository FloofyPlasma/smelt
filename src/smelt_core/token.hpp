#pragma once

#include "diagnostics.hpp"
#include <cstdint>
#include <string_view>

namespace smelt {
enum class TokenKind : std::uint8_t {
  Identifier,
  String,
  Integer,

  KwProject,
  KwProfile,
  KwTarget,
  KwDependency,
  KwSource,
  KwOn,
  KwTrue,
  KwFalse,

  LBrace,
  RBrace,
  LBracket,
  RBracket,
  LParen,
  RParen,
  Semicolon,
  Equals,
  Comma,
  Dot,

  EndOfFile,
  Invalid
};

struct Token {
  TokenKind kind = TokenKind::Invalid;
  std::string_view text;
  SourceLoc loc;
};

[[nodiscard]] auto tokenKindName(TokenKind kind) -> std::string_view;
} // namespace smelt

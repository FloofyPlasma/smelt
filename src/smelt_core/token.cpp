#include "token.hpp"

namespace smelt {
auto tokenKindName(TokenKind kind) -> std::string_view {
  switch (kind) {
  case TokenKind::Identifier:
    return "identifier";
  case TokenKind::String:
    return "string";
  case TokenKind::Integer:
    return "integer";
  case TokenKind::KwProject:
    return "'project'";
  case TokenKind::KwProfile:
    return "'profile'";
  case TokenKind::KwTarget:
    return "'target'";
  case TokenKind::KwDependency:
    return "'dependency'";
  case TokenKind::KwSource:
    return "'source'";
  case TokenKind::KwOn:
    return "'on'";
  case TokenKind::KwTrue:
    return "'true'";
  case TokenKind::KwFalse:
    return "'false'";
  case TokenKind::LBrace:
    return "'{'";
  case TokenKind::RBrace:
    return "'}'";
  case TokenKind::LBracket:
    return "'['";
  case TokenKind::RBracket:
    return "']'";
  case TokenKind::Semicolon:
    return "';'";
  case TokenKind::Equals:
    return "'='";
  case TokenKind::Comma:
    return "','";
  case TokenKind::Dot:
    return "'.'";
  case TokenKind::EndOfFile:
    return "end of file";
  case TokenKind::Invalid:
    return "invalid token";
  }
  return "unknown token";
}
} // namespace smelt

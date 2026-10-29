#include "lexer.hpp"
#include "diagnostics.hpp"
#include "token.hpp"
#include <cstddef>
#include <format>
#include <iterator>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace smelt {
namespace {
auto keywords() -> const std::unordered_map<std::string_view, TokenKind> & {
  static const std::unordered_map<std::string_view, TokenKind> table{
      {"project", TokenKind::KwProject},
      {"profile", TokenKind::KwProfile},
      {"target", TokenKind::KwTarget},
      {"dependency", TokenKind::KwDependency},
      {"source", TokenKind::KwSource},
      {"on", TokenKind::KwOn},
      {"true", TokenKind::KwTrue},
      {"false", TokenKind::KwFalse},
  };
  return table;
}

auto isDigit(const char c) -> bool { return c >= '0' && c <= '9'; }

auto isIdentStart(const char c) -> bool {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

auto isIdentChar(const char c) -> bool { return isIdentStart(c) || isDigit(c); }

auto isSpace(const char c) -> bool {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
} // namespace

Lexer::Lexer(const std::string_view source, Diagnostics &diags)
    : src_(source), diags_(diags) {}

auto Lexer::tokenize() -> std::vector<Token> {
  std::vector<Token> out;

  while (true) {
    skipTrivia();
    if (atEnd()) {
      out.push_back({TokenKind::EndOfFile, {}, here()});
      break;
    }
    out.push_back(lexToken());
  }

  return out;
}

auto Lexer::peek(std::size_t ahead) const -> char {
  const std::size_t i = pos_ + ahead;
  return i < src_.size() ? src_[i] : '\0';
}

auto Lexer::advance() -> char {
  const char c = src_[pos_++];
  if (c == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return c;
}

void Lexer::skipTrivia() {
  while (!atEnd()) {
    const char c = peek();

    if (isSpace(c)) {
      advance();
    } else if (c == '/' && peek(1) == '/') {
      while (!atEnd() && peek() != '\n')
        advance();
    } else if (c == '/' && peek(1) == '*') {
      const SourceLoc start = here();
      advance();
      advance();
      bool closed = false;
      while (!atEnd()) {
        if (peek() == '*' && peek(1) == '/') {
          advance();
          advance();
          closed = true;
          break;
        }
        advance();
      }
      if (!closed)
        diags_.error(start, "unterminated block comment");
    } else {
      break;
    }
  }
}

auto Lexer::make(TokenKind kind, std::size_t start, SourceLoc loc) const
    -> Token {
  return Token{kind, src_.substr(start, pos_ - start), loc};
}

auto Lexer::lexToken() -> Token {
  const std::size_t start = pos_;
  const SourceLoc loc = here();
  const char c = advance();

  if (isIdentStart(c))
    return lexWord(start, loc);
  if (isDigit(c) || (c == '-' && isDigit(peek())))
    return lexNumber(start, loc);
  if (c == '"')
    return lexString(start, loc);

  switch (c) {
  case '{':
    return make(TokenKind::LBrace, start, loc);
  case '}':
    return make(TokenKind::RBrace, start, loc);
  case '[':
    return make(TokenKind::LBracket, start, loc);
  case ']':
    return make(TokenKind::RBracket, start, loc);
  case '(':
    return make(TokenKind::LParen, start, loc);
  case ')':
    return make(TokenKind::RParen, start, loc);
  case ';':
    return make(TokenKind::Semicolon, start, loc);
  case '=':
    return make(TokenKind::Equals, start, loc);
  case ',':
    return make(TokenKind::Comma, start, loc);
  case '.':
    return make(TokenKind::Dot, start, loc);
  default:
    break;
  }

  diags_.error(loc, std::format("unexpected character '{}'", c));
  return make(TokenKind::Invalid, start, loc);
}

auto Lexer::lexWord(const std::size_t start, const SourceLoc loc) -> Token {
  while (!atEnd() && isIdentChar(peek()))
    advance();

  const std::string_view word = src_.substr(start, pos_ - start);
  if (const auto it = keywords().find(word); it != keywords().end())
    return make(it->second, start, loc);

  return make(TokenKind::Identifier, start, loc);
}

auto Lexer::lexNumber(const std::size_t start, const SourceLoc loc) -> Token {
  while (!atEnd() && isDigit(peek()))
    advance();

  if (!atEnd() && isIdentChar(peek())) {
    diags_.error(here(), "letter directly after a number");
    while (!atEnd() && isIdentChar(peek()))
      advance();
    return make(TokenKind::Invalid, start, loc);
  }

  return make(TokenKind::Integer, start, loc);
}

auto Lexer::lexString(std::size_t start, SourceLoc loc) -> Token {
  bool ok = true;

  while (!atEnd() && peek() != '"' && peek() != '\n') {
    if (peek() == '\\') {
      const SourceLoc escLoc = here();
      advance();
      if (atEnd())
        break;
      const char e = advance();
      if (e != 'n' && e != 't' && e != 'r' && e != '\\' && e != '"') {
        diags_.error(escLoc, std::format("unknown escape sequence '\\{}'", e));
        ok = false;
      }
    } else {
      advance();
    }
  }

  if (atEnd() || peek() != '"') {
    diags_.error(loc, "unterminated string");
    return make(TokenKind::Invalid, start, loc);
  }
  advance();

  return make(ok ? TokenKind::String : TokenKind::Invalid, start, loc);
}

auto decodeString(std::string_view quoted) -> std::string {
  std::string out;

  for (std::size_t i = 1; i + 1 < quoted.size(); ++i) {
    const char c = quoted[i];
    if (c != '\\') {
      out.push_back(c);
      continue;
    }

    ++i;
    switch (quoted[i]) {
    case 'n':
      out.push_back('\n');
      break;
    case 't':
      out.push_back('\t');
      break;
    case 'r':
      out.push_back('\r');
      break;
    default:
      out.push_back(quoted[i]);
      break;
    }
  }

  return out;
}
} // namespace smelt

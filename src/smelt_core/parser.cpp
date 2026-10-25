#include "parser.hpp"
#include "ast.hpp"
#include "diagnostics.hpp"
#include "lexer.hpp"
#include "token.hpp"
#include <optional>
#include <stdexcept>
#include <vector>

namespace smelt {
namespace {
class Parser {
public:
  Parser(const std::vector<Token> &tokens, Diagnostics &diags)
      : tokens_(tokens), diags_(diags) {}

  auto parseFile() -> ProjectFile {
    ProjectFile file;

    while (!check(TokenKind::EndOfFile)) {
      if (check(TokenKind::Invalid)) {
        advance();
        continue;
      }

      switch (peek().kind) {
      case TokenKind::KwProject:
        if (auto p = parseProjectDecl())
          file.projects.push_back(std::move(*p));
        break;
      case TokenKind::KwProfile:
        if (auto p = parseProfileDecl())
          file.profiles.push_back(std::move(*p));
        break;
      case TokenKind::KwTarget:
        if (auto t = parseTargetDecl())
          file.targets.push_back(std::move(*t));
        break;
      case TokenKind::KwDependency:
        if (auto d = parseDependencyDecl())
          file.dependencies.push_back(std::move(*d));
        break;
      case TokenKind::KwSource:
        diags_.error(peek().loc, std::string(tokenKindName(peek().kind)) +
                                     " is not allowed in project.smelt");
        skipDeclaration();
        break;
      default:
        diags_.error(peek().loc,
                     "expected 'project', 'profile', or 'target', found " +
                         std::string(tokenKindName(peek().kind)));
        skipDeclaration();
        break;
      }
    }

    if (file.projects.empty() && !diags_.hasErrors())
      diags_.error({1, 1}, "missing 'project' declaration");

    return file;
  }

private:
  [[nodiscard]] auto peek(std::size_t ahead = 0) const -> const Token & {
    const std::size_t i = pos_ + ahead;
    return i < tokens_.size() ? tokens_[i] : tokens_.back();
  }

  auto advance() -> const Token & {
    const Token &t = peek();
    if (pos_ + 1 < tokens_.size())
      ++pos_;
    return t;
  }

  [[nodiscard]] auto check(TokenKind kind) const -> bool {
    return peek().kind == kind;
  }

  auto accept(TokenKind kind) -> bool {
    if (!check(kind))
      return false;
    advance();
    return true;
  }

  auto expect(TokenKind kind, const char *what) -> bool {
    if (accept(kind))
      return true;
    diags_.error(peek().loc, std::string("expected ") + what + ", found " +
                                 std::string(tokenKindName(peek().kind)));
    return false;
  }

  void skipDeclaration() {
    while (!check(TokenKind::EndOfFile)) {
      if (accept(TokenKind::Semicolon))
        return;
      if (check(TokenKind::RBrace) || isTopLevelKeyword(peek().kind))
        return;
      advance();
    }
  }

  void skipToBlockEnd() {
    while (!check(TokenKind::EndOfFile) && !check(TokenKind::RBrace)) {
      if (isTopLevelKeyword(peek().kind))
        return;
      advance();
    }
  }

  static auto isTopLevelKeyword(TokenKind kind) -> bool {
    return kind == TokenKind::KwProject || kind == TokenKind::KwProfile ||
           kind == TokenKind::KwTarget || kind == TokenKind::KwDependency ||
           kind == TokenKind::KwSource;
  }

  auto parseProjectDecl() -> std::optional<ProjectDecl> {
    ProjectDecl decl;
    decl.loc = advance().loc;

    if (!check(TokenKind::String)) {
      expect(TokenKind::String, "a quoted project name");
      skipDeclaration();
      return std::nullopt;
    }
    decl.name = decodeString(advance().text);

    if (!parseAssignBlock(decl.assigns)) {
      return std::nullopt;
    }
    return decl;
  }

  auto parseProfileDecl() -> std::optional<ProfileDecl> {
    ProfileDecl decl;
    decl.loc = advance().loc;

    if (!check(TokenKind::Identifier)) {
      expect(TokenKind::Identifier, "a bare profile name");
      skipDeclaration();
      return std::nullopt;
    }
    decl.name = std::string(advance().text);

    if (!parseAssignBlock(decl.assigns)) {
      return std::nullopt;
    }
    return decl;
  }

  auto parseTargetDecl() -> std::optional<TargetDecl> {
    TargetDecl decl;
    decl.loc = advance().loc;

    if (!check(TokenKind::String)) {
      expect(TokenKind::String, "a quoted target name");
      skipDeclaration();
      return std::nullopt;
    }
    decl.name = decodeString(advance().text);

    if (!expect(TokenKind::LBrace, "'{'")) {
      skipDeclaration();
      return std::nullopt;
    }

    while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
      if (isTopLevelKeyword(peek().kind))
        break;

      if (check(TokenKind::KwOn)) {
        if (auto block = parseOnBlock())
          decl.onBlocks.push_back(std::move(*block));
      } else if (auto a = parseAssign()) {
        decl.assigns.push_back(std::move(*a));
      }
    }

    if (!expect(TokenKind::RBrace, "'}'"))
      return std::nullopt;
    return decl;
  }

  auto parseDependencyDecl() -> std::optional<DependencyDecl> {
    DependencyDecl decl;
    decl.loc = advance().loc;

    if (!check(TokenKind::String)) {
      expect(TokenKind::String, "a quoted dependency name");
      skipDeclaration();
      return std::nullopt;
    }
    decl.name = decodeString(advance().text);

    if (!parseAssignBlock(decl.assigns))
      return std::nullopt;
    return decl;
  }

  auto parseOnBlock() -> std::optional<OnBlock> {
    OnBlock block;
    block.loc = advance().loc;

    if (!check(TokenKind::Identifier)) {
      expect(TokenKind::Identifier, "a platform name");
      skipToBlockEnd();
      accept(TokenKind::RBrace);
      return std::nullopt;
    }
    block.platform = std::string(advance().text);

    if (!expect(TokenKind::LBrace, "'{'")) {
      skipToBlockEnd();
      accept(TokenKind::RBrace);
      return std::nullopt;
    }

    while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
      if (isTopLevelKeyword(peek().kind))
        break;
      if (auto a = parseAssign())
        block.assigns.push_back(std::move(*a));
    }

    if (!expect(TokenKind::RBrace, "'}'"))
      return std::nullopt;
    return block;
  }

  auto parseAssignBlock(std::vector<Assign> &out) -> bool {
    if (!expect(TokenKind::LBrace, "'{'")) {
      skipDeclaration();
      return false;
    }

    while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
      if (isTopLevelKeyword(peek().kind))
        break;
      if (auto a = parseAssign())
        out.push_back(std::move(*a));
    }

    return expect(TokenKind::RBrace, "'}'");
  }

  auto parseAssign() -> std::optional<Assign> {
    if (check(TokenKind::Invalid)) {
      advance();
      return std::nullopt;
    }

    if (!check(TokenKind::Identifier)) {
      diags_.error(peek().loc, "expected a setting name, found " +
                                   std::string(tokenKindName(peek().kind)));
      skipDeclaration();
      return std::nullopt;
    }

    Assign a;
    a.loc = peek().loc;
    a.key = std::string(advance().text);

    if (!expect(TokenKind::Equals, "'='")) {
      skipDeclaration();
      return std::nullopt;
    }

    auto value = parseValue();
    if (!value) {
      skipDeclaration();
      return std::nullopt;
    }
    a.value = std::move(*value);

    if (!expect(TokenKind::Semicolon, "';' after value")) {
      skipDeclaration();
      return std::nullopt;
    }
    return a;
  }

  auto parseValue() -> std::optional<Value> {
    Value v;
    v.loc = peek().loc;

    switch (peek().kind) {
    case TokenKind::String:
      v.kind = Value::Kind::String;
      v.text = decodeString(advance().text);
      return v;

    case TokenKind::Integer: {
      v.kind = Value::Kind::Integer;
      const std::string digits(advance().text);
      try {
        v.integer = std::stoll(digits);
      } catch (const std::out_of_range &) {
        diags_.error(v.loc, "integer out of range: " + digits);
        return std::nullopt;
      }
      return v;
    }

    case TokenKind::KwTrue:
    case TokenKind::KwFalse:
      v.kind = Value::Kind::Bool;
      v.boolean = advance().kind == TokenKind::KwTrue;
      return v;

    case TokenKind::Identifier:
      v.kind = Value::Kind::Word;
      v.text = std::string(advance().text);
      return v;

    case TokenKind::LBracket:
      return parseList(v);

    default:
      diags_.error(peek().loc, "expected a value, found " +
                                   std::string(tokenKindName(peek().kind)));
      return std::nullopt;
    }
  }

  auto parseList(Value &v) -> std::optional<Value> {
    v.kind = Value::Kind::List;
    advance();

    if (accept(TokenKind::RBracket))
      return v;

    while (true) {
      if (!check(TokenKind::String)) {
        diags_.error(peek().loc, "list items must be quoted strings, found " +
                                     std::string(tokenKindName(peek().kind)));
        return std::nullopt;
      }
      Value item;
      item.kind = Value::Kind::String;
      item.loc = peek().loc;
      item.text = decodeString(advance().text);
      v.items.push_back(std::move(item));

      if (accept(TokenKind::RBracket))
        return v;
      if (!expect(TokenKind::Comma, "',' or ']' in list"))
        return std::nullopt;
    }
  }

  std::vector<Token> tokens_;
  Diagnostics &diags_;
  std::size_t pos_ = 0;
};
} // namespace

auto parseProject(const std::vector<Token> &tokens, Diagnostics &diags)
    -> ProjectFile {
  Parser parser(tokens, diags);
  return parser.parseFile();
}
} // namespace smelt

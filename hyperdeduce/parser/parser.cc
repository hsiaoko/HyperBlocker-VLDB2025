#include "hyperdeduce/parser/parser.h"

#include <utility>

#include "hyperdeduce/parser/lexer.h"

namespace hyperdeduce {
namespace {

bool IsComparison(TokenKind kind) {
  switch (kind) {
  case TokenKind::kEq:
  case TokenKind::kNeq:
  case TokenKind::kLt:
  case TokenKind::kLe:
  case TokenKind::kGt:
  case TokenKind::kGe:
    return true;
  default:
    return false;
  }
}

class Parser {
public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

  StatusOr<std::vector<Rule>> ParseProgram() {
    std::vector<Rule> rules;
    while (Current().kind != TokenKind::kEnd) {
      auto rule = ParseRule();
      if (!rule.ok()) {
        return rule.status();
      }
      rules.push_back(rule.value());
    }
    return rules;
  }

private:
  const Token &Current() const { return tokens_[index_]; }

  const Token &Advance() { return tokens_[index_++]; }

  bool Match(TokenKind kind) {
    if (Current().kind == kind) {
      Advance();
      return true;
    }
    return false;
  }

  Status Error(const std::string &message) const {
    const Token &token = Current();
    return Status::Error("parse error at line " + std::to_string(token.line) +
                         " column " + std::to_string(token.column) + ": " +
                         message);
  }

  Status Expect(TokenKind kind, const std::string &what) {
    if (!Match(kind)) {
      return Error("expected " + what);
    }
    return Status::Ok();
  }

  StatusOr<Term> ParseTerm() {
    const Token &token = Current();
    switch (token.kind) {
    case TokenKind::kIdent:
      Advance();
      return Term::Variable(token.text);
    case TokenKind::kInt: {
      Advance();
      Constant constant;
      constant.type = DataType::kInt64;
      constant.text = token.text;
      constant.int_value = token.int_value;
      return Term::Const(constant);
    }
    case TokenKind::kDouble: {
      Advance();
      Constant constant;
      constant.type = DataType::kDouble;
      constant.text = token.text;
      constant.double_value = token.double_value;
      return Term::Const(constant);
    }
    case TokenKind::kString: {
      Advance();
      Constant constant;
      constant.type = DataType::kString;
      constant.text = token.text;
      return Term::Const(constant);
    }
    default:
      return Error("expected a term");
    }
  }

  StatusOr<Atom> ParseAtom() {
    if (Current().kind != TokenKind::kIdent) {
      return Error("expected a predicate name");
    }
    Atom atom;
    atom.predicate = Advance().text;
    if (Match(TokenKind::kLParen)) {
      if (Current().kind != TokenKind::kRParen) {
        while (true) {
          auto term = ParseTerm();
          if (!term.ok()) {
            return term.status();
          }
          atom.terms.push_back(term.value());
          if (!Match(TokenKind::kComma)) {
            break;
          }
        }
      }
      Status closed = Expect(TokenKind::kRParen, "')'");
      if (!closed.ok()) {
        return closed;
      }
    }
    return atom;
  }

  // A body literal is either an atom or an infix comparison. A comparison may
  // start with a variable (which also starts an atom) or a constant.
  StatusOr<Atom> ParseLiteral() {
    if (Current().kind == TokenKind::kIdent &&
        tokens_[index_ + 1].kind == TokenKind::kLParen) {
      return ParseAtom();
    }
    auto first = ParseTerm();
    if (!first.ok()) {
      return first.status();
    }
    if (IsComparison(Current().kind)) {
      const std::string op = Advance().text;
      auto second = ParseTerm();
      if (!second.ok()) {
        return second.status();
      }
      Atom comparison;
      comparison.predicate = op;
      comparison.terms.push_back(first.value());
      comparison.terms.push_back(second.value());
      return comparison;
    }
    // A bare identifier with no '(' and no comparison is a 0-ary atom.
    if (first.value().is_variable) {
      Atom atom;
      atom.predicate = first.value().variable;
      return atom;
    }
    return Error("expected a comparison operator after constant");
  }

  StatusOr<Rule> ParseRule() {
    auto head = ParseAtom();
    if (!head.ok()) {
      return head.status();
    }
    Rule rule;
    rule.head = head.value();
    if (Match(TokenKind::kImplies)) {
      while (true) {
        auto literal = ParseLiteral();
        if (!literal.ok()) {
          return literal.status();
        }
        rule.body.push_back(literal.value());
        if (!Match(TokenKind::kComma)) {
          break;
        }
      }
    }
    Status terminated = Expect(TokenKind::kDot, "'.'");
    if (!terminated.ok()) {
      return terminated;
    }
    return rule;
  }

  std::vector<Token> tokens_;
  std::size_t index_ = 0;
};

} // namespace

StatusOr<std::vector<Rule>> ParseProgram(const std::string &source) {
  auto tokens = Tokenize(source);
  if (!tokens.ok()) {
    return tokens.status();
  }
  Parser parser(tokens.value());
  return parser.ParseProgram();
}

} // namespace hyperdeduce
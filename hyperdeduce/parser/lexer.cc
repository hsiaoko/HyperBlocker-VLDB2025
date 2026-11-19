#include "hyperdeduce/parser/lexer.h"

#include <cctype>
#include <charconv>
#include <system_error>
#include <utility>

namespace hyperdeduce {
namespace {

bool IsIdentStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool IsIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool IsDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

class Lexer {
public:
  explicit Lexer(const std::string &source) : source_(source) {}

  StatusOr<std::vector<Token>> Run() {
    std::vector<Token> tokens;
    while (true) {
      SkipWhitespaceAndComments();
      if (pos_ >= source_.size()) {
        tokens.push_back(MakeToken(TokenKind::kEnd, ""));
        return tokens;
      }
      auto token = NextToken();
      if (!token.ok()) {
        return token.status();
      }
      tokens.push_back(token.value());
    }
  }

private:
  Token MakeToken(TokenKind kind, std::string text) {
    Token token;
    token.kind = kind;
    token.text = std::move(text);
    token.line = line_;
    token.column = column_;
    return token;
  }

  Status Error(const std::string &message) {
    return Status::Error("lex error at line " + std::to_string(line_) +
                         " column " + std::to_string(column_) + ": " + message);
  }

  char Peek(std::size_t offset = 0) const {
    const std::size_t index = pos_ + offset;
    return index < source_.size() ? source_[index] : '\0';
  }

  char Advance() {
    const char c = source_[pos_++];
    if (c == '\n') {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
    return c;
  }

  void SkipWhitespaceAndComments() {
    while (pos_ < source_.size()) {
      const char c = Peek();
      if (std::isspace(static_cast<unsigned char>(c)) != 0) {
        Advance();
      } else if (c == '%' || (c == '/' && Peek(1) == '/')) {
        while (pos_ < source_.size() && Peek() != '\n') {
          Advance();
        }
      } else {
        break;
      }
    }
  }

  StatusOr<Token> NextToken() {
    const std::size_t start_line = line_;
    const std::size_t start_column = column_;
    const char c = Peek();

    if (IsIdentStart(c)) {
      std::string text;
      while (pos_ < source_.size() && IsIdentChar(Peek())) {
        text += Advance();
      }
      Token token = MakeToken(TokenKind::kIdent, text);
      token.line = start_line;
      token.column = start_column;
      return token;
    }

    if (IsDigit(c) || (c == '-' && IsDigit(Peek(1)))) {
      return LexNumber(start_line, start_column);
    }

    if (c == '"') {
      return LexString(start_line, start_column);
    }

    Advance();
    TokenKind kind = TokenKind::kEnd;
    std::string text(1, c);
    switch (c) {
    case '(':
      kind = TokenKind::kLParen;
      break;
    case ')':
      kind = TokenKind::kRParen;
      break;
    case ',':
      kind = TokenKind::kComma;
      break;
    case '.':
      kind = TokenKind::kDot;
      break;
    case '=':
      kind = TokenKind::kEq;
      break;
    case ':':
      if (Peek() == '-') {
        Advance();
        kind = TokenKind::kImplies;
        text = ":-";
      } else {
        return Error("expected '-' after ':'");
      }
      break;
    case '!':
      if (Peek() == '=') {
        Advance();
        kind = TokenKind::kNeq;
        text = "!=";
      } else {
        return Error("expected '=' after '!'");
      }
      break;
    case '<':
      if (Peek() == '=') {
        Advance();
        kind = TokenKind::kLe;
        text = "<=";
      } else {
        kind = TokenKind::kLt;
      }
      break;
    case '>':
      if (Peek() == '=') {
        Advance();
        kind = TokenKind::kGe;
        text = ">=";
      } else {
        kind = TokenKind::kGt;
      }
      break;
    default:
      return Error(std::string("unexpected character '") + c + "'");
    }
    Token token = MakeToken(kind, text);
    token.line = start_line;
    token.column = start_column;
    return token;
  }

  StatusOr<Token> LexNumber(std::size_t start_line, std::size_t start_column) {
    std::string text;
    if (Peek() == '-') {
      text += Advance();
    }
    while (IsDigit(Peek())) {
      text += Advance();
    }
    bool is_double = false;
    if (Peek() == '.' && IsDigit(Peek(1))) {
      is_double = true;
      text += Advance();
      while (IsDigit(Peek())) {
        text += Advance();
      }
    }
    if (Peek() == 'e' || Peek() == 'E') {
      is_double = true;
      text += Advance();
      if (Peek() == '+' || Peek() == '-') {
        text += Advance();
      }
      if (!IsDigit(Peek())) {
        return Error("expected digits in exponent");
      }
      while (IsDigit(Peek())) {
        text += Advance();
      }
    }
    Token token;
    token.line = start_line;
    token.column = start_column;
    token.text = text;
    if (is_double) {
      token.kind = TokenKind::kDouble;
      const auto result =
          std::from_chars(text.data(), text.data() + text.size(), token.double_value);
      if (result.ec != std::errc{}) {
        return Error("invalid double literal '" + text + "'");
      }
    } else {
      token.kind = TokenKind::kInt;
      const auto result =
          std::from_chars(text.data(), text.data() + text.size(), token.int_value);
      if (result.ec != std::errc{}) {
        return Error("invalid integer literal '" + text + "'");
      }
    }
    return token;
  }

  StatusOr<Token> LexString(std::size_t start_line, std::size_t start_column) {
    Advance(); // opening quote
    std::string text;
    while (true) {
      if (pos_ >= source_.size()) {
        return Error("unterminated string literal");
      }
      const char c = Advance();
      if (c == '"') {
        break;
      }
      if (c == '\\') {
        if (pos_ >= source_.size()) {
          return Error("unterminated escape in string literal");
        }
        const char escaped = Advance();
        switch (escaped) {
        case '"':
          text += '"';
          break;
        case '\\':
          text += '\\';
          break;
        case 'n':
          text += '\n';
          break;
        case 't':
          text += '\t';
          break;
        default:
          return Error(std::string("unknown escape '\\") + escaped + "'");
        }
      } else {
        text += c;
      }
    }
    Token token = MakeToken(TokenKind::kString, text);
    token.line = start_line;
    token.column = start_column;
    return token;
  }

  const std::string &source_;
  std::size_t pos_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
};

} // namespace

StatusOr<std::vector<Token>> Tokenize(const std::string &source) {
  Lexer lexer(source);
  return lexer.Run();
}

} // namespace hyperdeduce
#include "mpt/asset_import/gclient_deps.h"

#include <vector>

#include "mpt/asset_import/source_lock.h"

namespace mpt::asset_import {
namespace {

constexpr int kMaxDepth = 64;

struct Value {
  enum class Kind { string, dict, list, literal } kind = Kind::literal;
  std::string text;  // string contents, or the literal's spelling
  std::vector<std::string> keys;  // dict keys (strings only)
  std::vector<Value> items;       // dict values or list items
};

struct Token {
  enum class Kind { string, ident, punct, end } kind = Kind::end;
  std::string text;
  std::size_t line = 0;
};

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  bool parse(std::map<std::string, Value>& assignments, std::string& error) {
    if (!next()) return failed(error);
    while (tok_.kind != Token::Kind::end) {
      if (tok_.kind != Token::Kind::ident) return fail(error, "expected a name");
      const std::string name = tok_.text;
      if (!next()) return failed(error);
      if (!is_punct("=")) return fail(error, "expected '=' after '" + name + "'");
      if (!next()) return failed(error);
      Value value;
      if (!expr(value, 0)) return failed(error);
      assignments[name] = std::move(value);
    }
    return true;
  }

 private:
  bool is_punct(std::string_view p) const { return tok_.kind == Token::Kind::punct && tok_.text == p; }

  bool fail(std::string& error, std::string message) {
    error_ = std::move(message);
    return failed(error);
  }
  bool failed(std::string& error) {
    error = "DEPS line " + std::to_string(tok_.line) + ": " + error_;
    return false;
  }
  bool set_error(std::string message) {
    error_ = std::move(message);
    return false;
  }

  bool next() {
    // Skip whitespace and comments.
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == '\n') {
        ++line_;
        ++pos_;
      } else if (c == ' ' || c == '\t' || c == '\r' || c == '\\') {
        ++pos_;  // a trailing backslash continues a line
      } else if (c == '#') {
        while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
      } else {
        break;
      }
    }
    tok_ = Token{};
    tok_.line = line_;
    if (pos_ >= text_.size()) return true;
    const char c = text_[pos_];
    if (c == '\'' || c == '"') {
      if (text_.substr(pos_, 3) == std::string(3, c)) return set_error("triple-quoted strings are not supported");
      ++pos_;
      tok_.kind = Token::Kind::string;
      while (true) {
        if (pos_ >= text_.size() || text_[pos_] == '\n') return set_error("unterminated string");
        const char ch = text_[pos_++];
        if (ch == c) break;
        if (ch == '\\') {
          if (pos_ >= text_.size()) return set_error("unterminated string");
          const char escaped = text_[pos_++];
          tok_.text.push_back(escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped);
        } else {
          tok_.text.push_back(ch);
        }
      }
      return true;
    }
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (c >= '0' && c <= '9')) {
      tok_.kind = Token::Kind::ident;
      while (pos_ < text_.size()) {
        const char ch = text_[pos_];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' || (ch >= '0' && ch <= '9') ||
              ch == '.')) {
          break;
        }
        tok_.text.push_back(ch);
        ++pos_;
      }
      return true;
    }
    if (std::string_view("{}[]():,+=").find(c) != std::string_view::npos) {
      tok_.kind = Token::Kind::punct;
      tok_.text = std::string(1, c);
      ++pos_;
      return true;
    }
    return set_error(std::string("unexpected character '") + c + "'");
  }

  bool expr(Value& out, int depth) {
    if (depth > kMaxDepth) return set_error("nested too deeply");
    if (!term(out, depth)) return false;
    while (is_punct("+")) {
      if (!next()) return false;
      Value rhs;
      if (!term(rhs, depth)) return false;
      if (out.kind != Value::Kind::string || rhs.kind != Value::Kind::string) {
        return set_error("'+' joins strings only");
      }
      out.text += rhs.text;
    }
    return true;
  }

  bool term(Value& out, int depth) {
    if (tok_.kind == Token::Kind::string) {
      out = Value{Value::Kind::string, {}, {}, {}};
      while (tok_.kind == Token::Kind::string) {  // adjacent literals concatenate
        out.text += tok_.text;
        if (!next()) return false;
      }
      return true;
    }
    if (is_punct("{")) {
      out = Value{Value::Kind::dict, {}, {}, {}};
      if (!next()) return false;
      while (!is_punct("}")) {
        Value key;
        if (!expr(key, depth + 1)) return false;
        if (key.kind != Value::Kind::string) return set_error("dictionary keys must be strings");
        if (!is_punct(":")) return set_error("expected ':'");
        if (!next()) return false;
        Value value;
        if (!expr(value, depth + 1)) return false;
        out.keys.push_back(std::move(key.text));
        out.items.push_back(std::move(value));
        if (is_punct(",")) {
          if (!next()) return false;
        } else if (!is_punct("}")) {
          return set_error("expected ',' or '}'");
        }
      }
      return next();
    }
    if (is_punct("[")) {
      out = Value{Value::Kind::list, {}, {}, {}};
      if (!next()) return false;
      while (!is_punct("]")) {
        Value item;
        if (!expr(item, depth + 1)) return false;
        out.items.push_back(std::move(item));
        if (is_punct(",")) {
          if (!next()) return false;
        } else if (!is_punct("]")) {
          return set_error("expected ',' or ']'");
        }
      }
      return next();
    }
    if (tok_.kind == Token::Kind::ident) {
      const std::string name = tok_.text;
      if (!next()) return false;
      if (name == "Var" || name == "Str") {
        if (!is_punct("(")) return set_error("expected '(' after " + name);
        if (!next()) return false;
        Value arg;
        if (!expr(arg, depth + 1)) return false;
        if (arg.kind != Value::Kind::string) return set_error(name + "() takes a string");
        if (!is_punct(")")) return set_error("expected ')'");
        if (!next()) return false;
        // gclient: Var('x') is the "{x}" placeholder, expanded from vars later.
        out = Value{Value::Kind::string, name == "Var" ? "{" + arg.text + "}" : arg.text, {}, {}};
        return true;
      }
      if (name == "True" || name == "False" || name == "None" || (name[0] >= '0' && name[0] <= '9')) {
        out = Value{Value::Kind::literal, name, {}, {}};
        return true;
      }
      return set_error("unsupported name '" + name + "'");
    }
    if (tok_.kind == Token::Kind::end) return set_error("unexpected end of file");
    return set_error("unexpected '" + tok_.text + "'");
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  std::size_t line_ = 1;
  Token tok_;
  std::string error_;
};

const Value* lookup(const Value& dict, std::string_view key) {
  for (std::size_t i = 0; i < dict.keys.size(); ++i) {
    if (dict.keys[i] == key) return &dict.items[i];
  }
  return nullptr;
}

// Expands "{name}" from vars (str.format rules: "{{" and "}}" are literal braces).
bool expand(std::string_view text, const std::map<std::string, std::string>& vars, std::string& out,
            std::string& error, int depth = 0) {
  if (depth > 8) {
    error = "DEPS: variables refer to each other too deeply.";
    return false;
  }
  out.clear();
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if ((c == '{' || c == '}') && i + 1 < text.size() && text[i + 1] == c) {
      out.push_back(c);
      ++i;
    } else if (c == '{') {
      const std::size_t end = text.find('}', i);
      if (end == std::string_view::npos) {
        error = "DEPS: unclosed '{' in '" + std::string(text) + "'.";
        return false;
      }
      const std::string name(text.substr(i + 1, end - i - 1));
      const auto var = vars.find(name);
      if (var == vars.end()) {
        error = "DEPS: unknown variable '" + name + "'.";
        return false;
      }
      std::string value;
      if (!expand(var->second, vars, value, error, depth + 1)) return false;
      out += value;
      i = end;
    } else {
      out.push_back(c);
    }
  }
  return true;
}

}  // namespace

std::optional<std::map<std::string, GclientDep>> parse_gclient_deps(std::string_view text, std::string& error) {
  std::map<std::string, Value> assignments;
  Parser parser(text);
  if (!parser.parse(assignments, error)) return std::nullopt;

  std::map<std::string, std::string> vars;
  if (const auto it = assignments.find("vars"); it != assignments.end()) {
    if (it->second.kind != Value::Kind::dict) {
      error = "DEPS: 'vars' is not a dictionary.";
      return std::nullopt;
    }
    for (std::size_t i = 0; i < it->second.keys.size(); ++i) vars[it->second.keys[i]] = it->second.items[i].text;
  }
  const auto deps = assignments.find("deps");
  if (deps == assignments.end() || deps->second.kind != Value::Kind::dict) {
    error = "DEPS has no 'deps' dictionary.";
    return std::nullopt;
  }

  std::map<std::string, GclientDep> out;
  const Value& dict = deps->second;
  for (std::size_t i = 0; i < dict.keys.size(); ++i) {
    std::string path;
    if (!expand(dict.keys[i], vars, path, error)) return std::nullopt;
    const Value& entry = dict.items[i];
    GclientDep dep;
    std::string raw_url;
    if (entry.kind == Value::Kind::string) {
      raw_url = entry.text;
    } else if (entry.kind == Value::Kind::dict) {
      const Value* type = lookup(entry, "dep_type");
      dep.cipd = type && type->kind == Value::Kind::string && type->text == "cipd";
      if (const Value* url = lookup(entry, "url"); url && url->kind == Value::Kind::string) raw_url = url->text;
    } else {
      error = "DEPS: the entry for '" + path + "' is neither a URL nor a dictionary.";
      return std::nullopt;
    }
    if (!raw_url.empty() && !expand(raw_url, vars, dep.url, error)) return std::nullopt;
    if (const std::size_t at = dep.url.rfind('@'); at != std::string::npos) {
      const std::string commit = dep.url.substr(at + 1);
      if (is_git_object_id(commit)) dep.commit = commit;
    }
    if (!out.emplace(path, std::move(dep)).second) {
      error = "DEPS lists '" + path + "' twice.";
      return std::nullopt;
    }
  }
  return out;
}

}  // namespace mpt::asset_import

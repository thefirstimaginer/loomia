#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace loomia {
namespace json {

// ---------------------------------------------------------------------------
// Leitura
// ---------------------------------------------------------------------------

const ValuePtr Value::get(const std::string& key) const {
  if (type != Type::Object) return nullptr;
  for (const auto& entry : object) {
    if (entry.first == key) return entry.second;
  }
  return nullptr;
}

double Value::num(const std::string& key, double fallback) const {
  const ValuePtr entry = get(key);
  return (entry && entry->isNumber()) ? entry->number : fallback;
}

std::string Value::str(const std::string& key, const std::string& fallback) const {
  const ValuePtr entry = get(key);
  return (entry && entry->isString()) ? entry->string : fallback;
}

bool Value::flag(const std::string& key, bool fallback) const {
  const ValuePtr entry = get(key);
  return (entry && entry->type == Type::Bool) ? entry->boolean : fallback;
}

const ValuePtr Value::item(size_t index) const {
  if (type != Type::Array || index >= array.size()) return nullptr;
  return array[index];
}

double Value::at(size_t index, double fallback) const {
  const ValuePtr entry = item(index);
  return (entry && entry->isNumber()) ? entry->number : fallback;
}

size_t Value::size() const {
  if (type == Type::Array) return array.size();
  if (type == Type::Object) return object.size();
  return 0;
}

ValuePtr makeNull() {
  auto value = std::make_shared<Value>();
  value->type = Type::Null;
  return value;
}

ValuePtr makeBool(bool flag) {
  auto value = std::make_shared<Value>();
  value->type = Type::Bool;
  value->boolean = flag;
  return value;
}

ValuePtr makeNumber(double number) {
  auto value = std::make_shared<Value>();
  value->type = Type::Number;
  value->number = number;
  return value;
}

ValuePtr makeString(const std::string& text) {
  auto value = std::make_shared<Value>();
  value->type = Type::String;
  value->string = text;
  return value;
}

ValuePtr makeArray() {
  auto value = std::make_shared<Value>();
  value->type = Type::Array;
  return value;
}

ValuePtr makeObject() {
  auto value = std::make_shared<Value>();
  value->type = Type::Object;
  return value;
}

void set(Value& object, const std::string& key, ValuePtr value) {
  if (object.type != Type::Object) object.type = Type::Object;
  for (auto& entry : object.object) {
    if (entry.first == key) {
      entry.second = std::move(value);
      return;
    }
  }
  object.object.emplace_back(key, std::move(value));
}

void push(Value& array, ValuePtr value) {
  if (array.type != Type::Array) array.type = Type::Array;
  array.array.push_back(std::move(value));
}

ValuePtr makeVec3(double x, double y, double z) {
  auto list = makeArray();
  push(*list, makeNumber(x));
  push(*list, makeNumber(y));
  push(*list, makeNumber(z));
  return list;
}

bool readVec3(const ValuePtr& value, double& x, double& y, double& z) {
  if (!value || !value->isArray() || value->array.size() < 3) return false;
  x = value->at(0);
  y = value->at(1);
  z = value->at(2);
  return true;
}

namespace {

class Parser {
 public:
  Parser(const std::string& text) : text_(text) {}

  ValuePtr run(std::string* error) {
    skipSpace();
    ValuePtr value = parseValue();
    if (!value) {
      if (error) *error = error_ + " (posição " + std::to_string(position_) + ")";
      return nullptr;
    }
    skipSpace();
    if (position_ != text_.size()) {
      if (error) *error = "conteúdo extra após o JSON (posição " + std::to_string(position_) + ")";
      return nullptr;
    }
    return value;
  }

 private:
  const std::string& text_;
  size_t position_ = 0;
  std::string error_;

  void skipSpace() {
    while (position_ < text_.size()) {
      const char c = text_[position_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        position_++;
      } else {
        break;
      }
    }
  }

  bool fail(const std::string& message) {
    if (error_.empty()) error_ = message;
    return false;
  }

  ValuePtr parseValue() {
    skipSpace();
    if (position_ >= text_.size()) return fail("fim inesperado"), nullptr;
    switch (text_[position_]) {
      case '{': return parseObject();
      case '[': return parseArray();
      case '"': return parseString();
      case 't':
        if (text_.compare(position_, 4, "true") == 0) { position_ += 4; return makeBool(true); }
        return fail("literal inválido"), nullptr;
      case 'f':
        if (text_.compare(position_, 5, "false") == 0) { position_ += 5; return makeBool(false); }
        return fail("literal inválido"), nullptr;
      case 'n':
        if (text_.compare(position_, 4, "null") == 0) { position_ += 4; return makeNull(); }
        return fail("literal inválido"), nullptr;
      default: return parseNumber();
    }
  }

  ValuePtr parseObject() {
    auto object = makeObject();
    position_++;  // {
    skipSpace();
    if (position_ < text_.size() && text_[position_] == '}') {
      position_++;
      return object;
    }
    while (position_ < text_.size()) {
      skipSpace();
      if (position_ >= text_.size() || text_[position_] != '"') return fail("chave esperada"), nullptr;
      ValuePtr key = parseString();
      if (!key) return nullptr;
      skipSpace();
      if (position_ >= text_.size() || text_[position_] != ':') return fail("':' esperado"), nullptr;
      position_++;
      ValuePtr value = parseValue();
      if (!value) return nullptr;
      set(*object, key->string, value);
      skipSpace();
      if (position_ >= text_.size()) return fail("objeto sem fechamento"), nullptr;
      if (text_[position_] == ',') { position_++; continue; }
      if (text_[position_] == '}') { position_++; return object; }
      return fail("',' ou '}' esperado"), nullptr;
    }
    return fail("objeto sem fechamento"), nullptr;
  }

  ValuePtr parseArray() {
    auto array = makeArray();
    position_++;  // [
    skipSpace();
    if (position_ < text_.size() && text_[position_] == ']') {
      position_++;
      return array;
    }
    while (position_ < text_.size()) {
      ValuePtr value = parseValue();
      if (!value) return nullptr;
      push(*array, value);
      skipSpace();
      if (position_ >= text_.size()) return fail("array sem fechamento"), nullptr;
      if (text_[position_] == ',') { position_++; continue; }
      if (text_[position_] == ']') { position_++; return array; }
      return fail("',' ou ']' esperado"), nullptr;
    }
    return fail("array sem fechamento"), nullptr;
  }

  static void appendUtf8(std::string& out, unsigned int code) {
    if (code <= 0x7F) {
      out += static_cast<char>(code);
    } else if (code <= 0x7FF) {
      out += static_cast<char>(0xC0 | ((code >> 6) & 0x1F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
      out += static_cast<char>(0xE0 | ((code >> 12) & 0x0F));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    }
  }

  ValuePtr parseString() {
    position_++;  // "
    std::string out;
    while (position_ < text_.size()) {
      const char c = text_[position_++];
      if (c == '"') return makeString(out);
      if (c != '\\') { out += c; continue; }
      if (position_ >= text_.size()) return fail("escape incompleto"), nullptr;
      const char escape = text_[position_++];
      switch (escape) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          if (position_ + 4 > text_.size()) return fail("\\u incompleto"), nullptr;
          unsigned int code = static_cast<unsigned int>(std::strtoul(text_.substr(position_, 4).c_str(), nullptr, 16));
          position_ += 4;
          appendUtf8(out, code);
          break;
        }
        default: return fail("escape inválido"), nullptr;
      }
    }
    return fail("string sem fechamento"), nullptr;
  }

  ValuePtr parseNumber() {
    const size_t start = position_;
    if (position_ < text_.size() && (text_[position_] == '-' || text_[position_] == '+')) position_++;
    while (position_ < text_.size()) {
      const char c = text_[position_];
      if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
        position_++;
      } else {
        break;
      }
    }
    if (start == position_) return fail("número esperado"), nullptr;
    const std::string token = text_.substr(start, position_ - start);
    char* end = nullptr;
    const double number = std::strtod(token.c_str(), &end);
    if (!end || *end != '\0') return fail("número inválido"), nullptr;
    return makeNumber(number);
  }
};

void writeString(std::ostringstream& out, const std::string& text) {
  out << '"';
  for (char c : text) {
    switch (c) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
          out << buffer;
        } else {
          out << c;
        }
    }
  }
  out << '"';
}

void writeNumber(std::ostringstream& out, double number) {
  if (!std::isfinite(number)) {
    out << "0";
    return;
  }
  if (number == std::floor(number) && std::fabs(number) < 1e15) {
    out << static_cast<long long>(number);
    return;
  }
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.6f", number);
  std::string text(buffer);
  while (!text.empty() && text.back() == '0') text.pop_back();
  if (!text.empty() && text.back() == '.') text.pop_back();
  out << text;
}

void writeValue(std::ostringstream& out, const Value& value, bool pretty, int depth) {
  const std::string pad = pretty ? std::string((depth + 1) * 2, ' ') : std::string();
  const std::string padEnd = pretty ? std::string(depth * 2, ' ') : std::string();
  switch (value.type) {
    case Type::Null: out << "null"; break;
    case Type::Bool: out << (value.boolean ? "true" : "false"); break;
    case Type::Number: writeNumber(out, value.number); break;
    case Type::String: writeString(out, value.string); break;
    case Type::Array: {
      if (value.array.empty()) { out << "[]"; break; }
      out << '[';
      for (size_t i = 0; i < value.array.size(); i++) {
        if (i) out << ',';
        if (pretty) out << '\n' << pad;
        writeValue(out, *value.array[i], pretty, depth + 1);
      }
      if (pretty) out << '\n' << padEnd;
      out << ']';
      break;
    }
    case Type::Object: {
      if (value.object.empty()) { out << "{}"; break; }
      out << '{';
      for (size_t i = 0; i < value.object.size(); i++) {
        if (i) out << ',';
        if (pretty) out << '\n' << pad;
        writeString(out, value.object[i].first);
        out << (pretty ? ": " : ":");
        writeValue(out, *value.object[i].second, pretty, depth + 1);
      }
      if (pretty) out << '\n' << padEnd;
      out << '}';
      break;
    }
  }
}

}  // namespace

ValuePtr parse(const std::string& text, std::string* error) {
  Parser parser(text);
  return parser.run(error);
}

std::string write(const Value& value, bool pretty) {
  std::ostringstream out;
  writeValue(out, value, pretty, 0);
  return out.str();
}

std::string writeCompact(const Value& value) {
  return write(value, false);
}

}  // namespace json
}  // namespace loomia

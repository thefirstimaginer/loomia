// JSON mínimo (parser + writer) para o `.loom` e o protocolo da sala.
//
// Sem dependências: cobre objetos, arrays, strings (com escapes e \uXXXX),
// números, true/false/null. É recursivo e tolerante a espaços.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace loomia {
namespace json {

struct Value;
using ValuePtr = std::shared_ptr<Value>;

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value {
  Type type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string string;
  std::vector<ValuePtr> array;
  std::vector<std::pair<std::string, ValuePtr>> object;

  // ---- leitura segura (com padrão) -------------------------------------
  bool isObject() const { return type == Type::Object; }
  bool isArray() const { return type == Type::Array; }
  bool isNumber() const { return type == Type::Number; }
  bool isString() const { return type == Type::String; }

  const ValuePtr get(const std::string& key) const;
  double num(const std::string& key, double fallback = 0) const;
  std::string str(const std::string& key, const std::string& fallback = "") const;
  bool flag(const std::string& key, bool fallback = false) const;

  double at(size_t index, double fallback = 0) const;
  const ValuePtr item(size_t index) const;
  size_t size() const;
};

/** Constrói valores para serializar. */
ValuePtr makeNull();
ValuePtr makeBool(bool value);
ValuePtr makeNumber(double value);
ValuePtr makeString(const std::string& value);
ValuePtr makeArray();
ValuePtr makeObject();

void set(Value& object, const std::string& key, ValuePtr value);
void push(Value& array, ValuePtr value);

/** Converte vetores de 3 números (usado pelo `.loom`). */
ValuePtr makeVec3(double x, double y, double z);
bool readVec3(const ValuePtr& value, double& x, double& y, double& z);

/** Analisa o texto; devolve nullptr e preenche `error` se inválido. */
ValuePtr parse(const std::string& text, std::string* error = nullptr);

/** Serializa (pretty por padrão; compacto para a rede). */
std::string write(const Value& value, bool pretty = true);
std::string writeCompact(const Value& value);

}  // namespace json
}  // namespace loomia

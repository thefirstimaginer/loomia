#include "args_parse.h"

#include <cstdlib>

namespace loomia {

namespace {

std::string decodeComponent(const std::string& value) {
  std::string out;
  for (size_t index = 0; index < value.size(); index++) {
    if (value[index] == '%' && index + 2 < value.size()) {
      const int code = static_cast<int>(std::strtol(value.substr(index + 1, 2).c_str(), nullptr, 16));
      out += static_cast<char>(code);
      index += 2;
    } else if (value[index] == '+') {
      out += ' ';
    } else {
      out += value[index];
    }
  }
  return out;
}

void applyPair(LaunchInfo& info, const std::string& key, const std::string& rawValue) {
  std::string value = decodeComponent(rawValue);
  // Remove espaços/quebras de linha que vêm do arquivo de link ou do shell
  // ("map=baseplate\n" gerava URL inválida no curl).
  while (!value.empty()) {
    const char last = value.back();
    if (last != '\r' && last != '\n' && last != '\t' && last != ' ') break;
    value.pop_back();
  }
  if (key == "server") info.server = value;
  else if (key == "token") info.token = value;
  else if (key == "map" || key == "mapId" || key == "map_id") info.mapId = value;
  else if (key == "origin") info.origin = value;
}

}  // namespace

LaunchInfo parseLaunchText(const std::string& text) {
  LaunchInfo info;
  std::string body = text;

  // Tira `loomia://launch?` / `loomia:launch?` e fica só com a query.
  const size_t queryStart = body.find('?');
  if (queryStart != std::string::npos) {
    body = body.substr(queryStart + 1);
  }

  // Aceita '&' ou espaços como separadores (o handler do OS entrega
  // `k=v k=v`, o site entrega `k=v&k=v`).
  std::string current;
  for (size_t index = 0; index <= body.size(); index++) {
    const bool end = index == body.size();
    const char c = end ? '&' : body[index];
    if (c != '&' && c != ' ') {
      current += c;
      continue;
    }
    if (!current.empty()) {
      const size_t equals = current.find('=');
      if (equals != std::string::npos) {
        applyPair(info, current.substr(0, equals), current.substr(equals + 1));
      }
      current.clear();
    }
  }

  info.valid = !info.server.empty() && !info.token.empty();
  return info;
}

}  // namespace loomia

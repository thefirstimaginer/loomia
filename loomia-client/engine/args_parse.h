// Parser do link `loomia://launch?...` (e do formato `chave=valor`).
#pragma once

#include <string>

namespace loomia {

struct LaunchInfo {
  std::string server;  // host:porta
  std::string token;
  std::string mapId;
  std::string origin;
  bool valid = false;
};

/** Analisa o texto do link. Aceita `loomia://...`, `loomia:...` e `a=b c=d`. */
LaunchInfo parseLaunchText(const std::string& text);

}  // namespace loomia

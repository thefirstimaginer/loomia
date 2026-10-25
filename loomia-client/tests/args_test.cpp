// Verifica o parser do link loomia:// usado pelo cliente.
#include <cstdio>
#include <string>

#include "args_parse.h"

int main() {
  struct Case {
    const char* text;
    bool valid;
    const char* server;
    const char* token;
    const char* map;
  };
  const Case cases[] = {
      {"loomia://launch?server=127.0.0.1:7800&token=abc&map=baseplate&origin=http://127.0.0.1:8090", true,
       "127.0.0.1:7800", "abc", "baseplate"},
      {"loomia://launch?server=10.0.0.2:7801&token=xyz", true, "10.0.0.2:7801", "xyz", ""},
      {"loomia://launch?server=127.0.0.1:7800&token=a%2Bb&map=x", true, "127.0.0.1:7800", "a+b", "x"},
      {"server=127.0.0.1:7802 token=tok map=m", true, "127.0.0.1:7802", "tok", "m"},
  };
  int failures = 0;
  for (const Case& entry : cases) {
    const loomia::LaunchInfo info = loomia::parseLaunchText(entry.text);
    const bool ok = info.valid == entry.valid && info.server == entry.server && info.token == entry.token &&
                    info.mapId == entry.map;
    std::printf("%s %-58s -> server=%s token=%s map=%s\n", ok ? "ok  " : "FAIL", entry.text,
                info.server.c_str(), info.token.c_str(), info.mapId.c_str());
    if (!ok) failures++;
  }
  return failures == 0 ? 0 : 1;
}

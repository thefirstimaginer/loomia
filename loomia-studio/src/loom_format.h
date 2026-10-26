// Formato .loom versão 1 — arquivo JSON versionado compartilhado por editor,
// servidor e cliente.
//
// Estrutura (ver docs/loom-format.md):
//   format/version  — identificação e compatibilidade
//   settings        — spawn, skyColor
//   objects[]       — blocos/partes: id, type, shape, transform, appearance,
//                     physics
//   scripts[]       — scripts Lua com id, language, target e source (inline
//                     ou arquivo externo)
//   logic[]         — eventos/ações estruturados referenciando objetos por id
//
// O editor salva sempre no formato v1 e carrega tanto v1 quanto o formato
// legado (blocks/spawn/sky_color), migrando automaticamente.
#pragma once

#include <string>
#include <vector>

#include "world.h"

namespace loomia {

// Representação intermediária do formato v1 (todos os campos do arquivo).
struct LoomObject {
  std::string id;
  std::string type = "part";
  std::string shape = "box";  // box | sphere | cylinder
  Vec3 position{0, 0, 0};
  Vec3 rotation{0, 0, 0};     // graus, convenção ZYX intrínseca
  Vec3 size{1, 1, 1};         // box: extensões; sphere: diâmetro; cylinder: (d, h, d)
  Color3 color{200, 200, 200};
  std::string material = "plastic";
  bool anchored = true;
  bool collision = true;
  float mass = 1.0f;
};

struct LoomScript {
  std::string id;
  std::string language = "lua";
  std::string target = "server";  // server | client | both
  std::string source;             // código inline
  std::string file;               // ou referência a arquivo externo
};

struct LoomLogicEvent {
  std::string id;
  std::string event = "onTouch";  // nome do evento suportado pelo jogo
  std::string objectId;           // objeto alvo (deve existir)
  std::string action = "none";    // ação suportada pelo jogo
  std::string targetId;           // objeto de destino da ação (opcional)
  double value = 0;
};

struct LoomDocument {
  int version = 1;
  std::string id;
  std::string name;
  std::string description;
  Vec3 spawn{0, 1.5f, 6};
  Color3 sky{150, 200, 240};
  std::vector<LoomObject> objects;
  std::vector<LoomScript> scripts;
  std::vector<LoomLogicEvent> logic;
};

// ---- validação compartilhável (editor e servidor usam as mesmas regras) ----
struct LoomIssue {
  bool error = true;       // false = aviso
  std::string message;
};

// Valida o documento: ids únicos, referências existentes, valores em faixa.
std::vector<LoomIssue> validateLoom(const LoomDocument& doc);

// ---- conversão World <-> LoomDocument (mantém o jogo e o editor unificados) ----
LoomDocument worldToLoom(const World& world);
World loomToWorld(const LoomDocument& doc);

// ---- serialização v1 ----
bool loadLoom(const std::string& path, LoomDocument& doc, std::vector<LoomIssue>& issues, std::string& error);
bool saveLoomV1(const std::string& path, const LoomDocument& doc, std::string& error);
std::string serializeLoom(const LoomDocument& doc);

// Abre qualquer versão: v1 direto; legado migrado para v1.
bool loadLoomAny(const std::string& path, LoomDocument& doc, std::vector<LoomIssue>& issues, std::string& error,
                 bool& migrated);

}  // namespace loomia

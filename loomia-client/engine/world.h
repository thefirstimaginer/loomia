// Formato de mapa do Loomia (`.loom`) — compartilhado pelo cliente e pelo studio.
//
// O arquivo é JSON:
//
//   {
//     "id": "baseplate", "name": "Baseplate", "description": "...",
//     "spawn": [0, 1, 4], "sky_color": [150, 200, 240],
//     "blocks": [
//       { "position": [x,y,z], "size": [w,h,d], "color": [r,g,b],
//         "material": "grass", "collidable": true }
//     ],
//     "items": [  { "type": "coin", "position": [...], "data": {...} } ],
//     "npcs":  [  { "name": "Guarda", "position": [...], "script": "npc_guard" } ],
//     "scripts": { "local": "lua", "global": "lua", "physics": "lua" },
//     "version": 1
//   }
//
// O cliente só precisa de `spawn`, `sky_color` e `blocks`; itens/NPCs/scripts
// são opcionais e ignorados com segurança por versões antigas.
#pragma once

#include <string>
#include <vector>

#include "math.h"

namespace loomia {

enum class Material { Plastic, Grass, Wood, Brick, Stone, Neon };

const char* materialName(Material material);
Material materialFromName(const std::string& name);

struct Block {
  Vec3 position{0, 0, 0};
  Vec3 size{1, 1, 1};
  Color3 color{200, 200, 200};
  Material material = Material::Plastic;
  bool collidable = true;
};

/** Item decorativo/coletável colocado no mapa. */
struct Item {
  std::string type = "coin";
  Vec3 position{0, 0, 0};
  float scale = 1.0f;
};

/** NPC com nome e, opcionalmente, um script associado. */
struct Npc {
  std::string name = "NPC";
  Vec3 position{0, 0, 0};
  std::string script;
};

/** Evento/script Lua do mapa (executado pelo servidor; o studio só armazena). */
struct Scripts {
  std::string local;    // eventos locais (ao entrar, ao interagir...)
  std::string global;   // eventos globais (tick, jogador entrou/saiu)
  std::string action;   // scripting de ação
  std::string physics;  // regras de física (gravidade, colisão especial)
};

struct World {
  std::string id;
  std::string name;
  std::string description;
  Vec3 spawn{0, 1, 4};
  Color3 sky{150, 200, 240};
  std::vector<Block> blocks;
  std::vector<Item> items;
  std::vector<Npc> npcs;
  Scripts scripts;
  int version = 1;

  /** Índice do bloco sob um ponto (ou -1). Usado pelo editor do studio. */
  int blockAt(Vec3 point) const;

  /** Caixa de colisão (min/max) de um bloco — mesma conta do servidor. */
  static void bounds(const Block& block, Vec3& min, Vec3& max);
};

/** Lê o `.loom`; devolve false e preenche `error` em caso de JSON inválido. */
bool loadWorld(const std::string& path, World& world, std::string& error);

/** Lê a partir de uma string (usado no download pela rede). */
bool parseWorld(const std::string& json, World& world, std::string& error);

/** Serializa para o formato `.loom` (indentado, estável para versionamento). */
std::string serializeWorld(const World& world);

/** Grava o arquivo; devolve false e preenche `error` se não conseguir. */
bool saveWorld(const std::string& path, const World& world, std::string& error);

}  // namespace loomia

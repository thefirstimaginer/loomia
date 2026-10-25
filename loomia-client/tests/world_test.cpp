// Testes do formato `.loom` (parser, serialização e ida-e-volta).
//
//   bin/loomia-tests
//
// Sem framework: imprime cada verificação e devolve código != 0 se falhar.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "json.h"
#include "world.h"

using namespace loomia;

static int passed = 0;
static int failed = 0;

static void check(const std::string& name, bool condition, const std::string& detail = "") {
  if (condition) {
    passed++;
    std::printf("  ok   %s\n", name.c_str());
  } else {
    failed++;
    std::printf("  FAIL %s %s\n", name.c_str(), detail.c_str());
  }
}

static std::string readFile(const std::string& path) {
  std::ifstream file(path);
  if (!file) return "";
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

int main(int argc, char** argv) {
  const std::string sample = argc > 1 ? argv[1] : "data/worlds/baseplate.loom";

  // ---- JSON básico -------------------------------------------------------
  std::printf("json\n");
  std::string error;
  auto value = json::parse(R"({"a":1,"b":[true,null,"x\n\"y\""],"c":{"d":2.5}})", &error);
  check("parse de objeto", value != nullptr, error);
  check("número lido", value && value->num("a") == 1);
  const auto list = value ? value->get("b") : nullptr;
  const auto third = list ? list->item(2) : nullptr;
  check("string com escape", third && third->type == loomia::json::Type::String && third->string == "x\n\"y\"");

  auto broken = json::parse("{\"a\": }", &error);
  check("JSON inválido é recusado", broken == nullptr, error);

  auto vec = json::makeVec3(1, 2, 3);
  check("serializa vetor", json::writeCompact(*vec) == "[1,2,3]", json::writeCompact(*vec));

  // ---- .loom real do repositório ----------------------------------------
  std::printf("mapa real: %s\n", sample.c_str());
  World world;
  const bool loaded = loadWorld(sample, world, error);
  check("carrega o .loom", loaded, error);
  if (loaded) {
    check("tem blocos", !world.blocks.empty(), std::to_string(world.blocks.size()));
    check("tem spawn", world.spawn.y > 0 || world.spawn.x != 0 || world.spawn.z != 0);

    Vec3 min, max;
    World::bounds(world.blocks.front(), min, max);
    check("caixa do bloco coerente", max.x > min.x && max.y > min.y && max.z > min.z);

    // blocoAt: um ponto no centro do primeiro bloco acha índice >= 0
    const int hit = world.blockAt(world.blocks.front().position);
    check("blockAt encontra o bloco", hit >= 0, std::to_string(hit));
    check("blockAt no vazio devolve -1", world.blockAt({99999, 99999, 99999}) == -1);

    // Ida e volta: serializar -> parsear -> mesmos dados
    const std::string text = serializeWorld(world);
    World round;
    check("serializa e reparseia", parseWorld(text, round, error), error);
    check("mesma quantidade de blocos", round.blocks.size() == world.blocks.size());
    check("mesmo spawn", std::fabs(round.spawn.x - world.spawn.x) < 0.001f &&
                             std::fabs(round.spawn.y - world.spawn.y) < 0.001f &&
                             std::fabs(round.spawn.z - world.spawn.z) < 0.001f);
    check("mesma cor do céu", round.sky.r == world.sky.r && round.sky.g == world.sky.g &&
                                  round.sky.b == world.sky.b);
    if (!world.blocks.empty()) {
      const Block& a = world.blocks.front();
      const Block& b = round.blocks.front();
      check("mesmo material", a.material == b.material);
      check("mesmo tamanho", std::fabs(a.size.x - b.size.x) < 0.001f &&
                                  std::fabs(a.size.y - b.size.y) < 0.001f &&
                                  std::fabs(a.size.z - b.size.z) < 0.001f);
    }
  }

  // ---- Mapa do studio (itens, NPCs, scripts) ----------------------------
  std::printf("mapa do studio\n");
  const std::string studioText = R"LOOM({
    "id": "studio", "name": "Studio", "spawn": [0, 2, 0], "sky_color": [10, 20, 30],
    "blocks": [{"position": [0, -1, 0], "size": [20, 2, 20], "color": [90, 160, 70], "material": "grass"}],
    "items": [{"type": "coin", "position": [1, 1, 1]}],
    "npcs": [{"name": "Guarda", "position": [3, 1, 3], "script": "guarda"}],
    "scripts": {"local": "print('oi')", "physics": "gravity = 120"}
  }
  )LOOM";
  World studio;
  check("parseia itens/NPCs/scripts", parseWorld(studioText, studio, error), error);
  check("1 item", studio.items.size() == 1);
  check("1 npc com nome", studio.npcs.size() == 1 && studio.npcs[0].name == "Guarda");
  check("script local guardado", studio.scripts.local == "print('oi')", studio.scripts.local);
  check("script de física guardado", studio.scripts.physics == "gravity = 120");

  const std::string studioOut = serializeWorld(studio);
  World studioRound;
  check("ida e volta do studio", parseWorld(studioOut, studioRound, error), error);
  check("scripts preservados", studioRound.scripts.local == studio.scripts.local);
  check("NPCs preservados", studioRound.npcs.size() == 1 && studioRound.npcs[0].script == "guarda");

  // ---- Mapa sem blocos é rejeitado -------------------------------------
  World empty;
  check("mapa sem blocos é recusado", !parseWorld(R"({"id":"x"})", empty, error), error);

  std::printf("\n%d passaram, %d falharam\n", passed, failed);
  return failed == 0 ? 0 : 1;
}

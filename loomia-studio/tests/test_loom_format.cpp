// Testes do formato .loom v1: carga/salva, migração, validação, ida e volta.
#include "../src/loom_format.h"

#include <cstdio>
#include <fstream>
#include <cassert>

using namespace loomia;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) { std::printf("  ✓ %s\n", msg); } else { std::printf("  ✗ %s\n", msg); ++failures; } } while (0)

static bool writeFile(const std::string& path, const std::string& text) {
  std::ofstream file(path, std::ios::trunc);
  file << text;
  return true;
}

int main() {
  std::printf("== 1. mapa v1 sem scripts ==\n");
  {
    const char* json = R"({
  "format": "loom", "version": 1, "id": "baseplate", "name": "Baseplate",
  "settings": {"spawn": [0, 1.5, 6], "skyColor": [150, 200, 240]},
  "objects": [
    {"id": "floor", "type": "part", "shape": "box",
     "transform": {"position": [0, -1, 0], "rotation": [0, 0, 0], "size": [48, 1, 48]},
     "appearance": {"color": [60, 200, 170], "material": "stone"},
     "physics": {"anchored": true, "collision": true, "mass": 1}}
  ],
  "scripts": [], "logic": []
})";
    writeFile("/tmp/t1.loom", json);
    LoomDocument doc;
    std::vector<LoomIssue> issues;
    std::string error;
    bool migrated = false;
    CHECK(loadLoomAny("/tmp/t1.loom", doc, issues, error, migrated), "carrega v1");
    CHECK(!migrated, "não marca como migrado");
    CHECK(doc.objects.size() == 1 && doc.objects[0].id == "floor", "objeto 'floor' lido");
    CHECK(doc.scripts.empty(), "sem scripts");
    CHECK(validateLoom(doc).empty(), "sem problemas de validação");

    World world = loomToWorld(doc);
    CHECK(world.blocks.size() == 1 && world.blocks[0].collidable, "converte para World");

    // ida e volta: salvar e recarregar
    std::string err;
    CHECK(saveLoomV1("/tmp/t1b.loom", doc, err), "salva v1");
    LoomDocument doc2;
    std::vector<LoomIssue> issues2;
    CHECK(loadLoom("/tmp/t1b.loom", doc2, issues2, err), "recarrega");
    CHECK(doc2.objects.size() == doc.objects.size() && doc2.objects[0].id == "floor", "round-trip: objetos");
    CHECK(doc2.spawn.x == doc.spawn.x && doc2.sky.r == doc.sky.r, "round-trip: settings");
    CHECK(doc2.scripts.size() == doc.scripts.size(), "round-trip: scripts");
  }

  std::printf("== 2. mapa v1 com scripts e lógica ==\n");
  {
    const char* json = R"({
  "format": "loom", "version": 1, "id": "tramp", "name": "Trampolim",
  "settings": {"spawn": [0, 2, 0], "skyColor": [40, 44, 60]},
  "objects": [
    {"id": "pad", "type": "part", "shape": "cylinder",
     "transform": {"position": [0, 0.5, 0], "rotation": [0, 0, 0], "size": [3, 1, 3]},
     "appearance": {"color": [230, 190, 40], "material": "neon"},
     "physics": {"anchored": true, "collision": true, "mass": 1}}
  ],
  "scripts": [
    {"id": "boost", "language": "lua", "target": "server",
     "source": "function onTouch(part) part.velocity = Vector3.new(0, 50, 0) end"}
  ],
  "logic": [
    {"id": "l1", "event": "onTouch", "objectId": "pad", "action": "teleport", "targetId": "pad", "value": 10}
  ]
})";
    writeFile("/tmp/t2.loom", json);
    LoomDocument doc;
    std::vector<LoomIssue> issues;
    std::string error;
    bool migrated = false;
    CHECK(loadLoomAny("/tmp/t2.loom", doc, issues, error, migrated), "carrega com scripts");
    CHECK(doc.scripts.size() == 1 && doc.scripts[0].language == "lua", "script lua lido");
    CHECK(doc.logic.size() == 1 && doc.logic[0].objectId == "pad", "lógica referenciando 'pad'");
    CHECK(validateLoom(doc).empty(), "referências válidas");

    World world = loomToWorld(doc);
    CHECK(!world.scripts.local.empty() || doc.scripts[0].target == "server", "scripts preservados no World");
  }

  std::printf("== 3. formato legado migrado ==\n");
  {
    const char* json = R"({
  "id": "old", "name": "Antigo", "spawn": [0, 1, 4], "sky_color": [150, 200, 240],
  "blocks": [{"position": [0, 0, 0], "size": [4, 2, 4], "color": [190, 70, 60], "material": "brick"}]
})";
    writeFile("/tmp/t3.loom", json);
    LoomDocument doc;
    std::vector<LoomIssue> issues;
    std::string error;
    bool migrated = false;
    CHECK(loadLoomAny("/tmp/t3.loom", doc, issues, error, migrated), "carrega legado");
    CHECK(migrated, "marcado como migrado");
    CHECK(doc.objects.size() == 1, "bloco virou objeto");
    CHECK(doc.objects[0].material == "brick", "material preservado");
  }

  std::printf("== 4. inválidos ==\n");
  {
    LoomDocument doc;
    std::vector<LoomIssue> issues;
    std::string error;
    bool migrated = false;

    writeFile("/tmp/t4a.loom", "{\"format\": \"loom\", \"version\": 99, \"objects\": []}");
    CHECK(!loadLoomAny("/tmp/t4a.loom", doc, issues, error, migrated), "versão futura rejeitada");
    CHECK(error.find("versão") != std::string::npos, "mensagem clara de versão");

    writeFile("/tmp/t4b.loom", "{\"format\": \"zip\", \"version\": 1}");
    CHECK(!loadLoomAny("/tmp/t4b.loom", doc, issues, error, migrated), "formato errado rejeitado");

    writeFile("/tmp/t4c.loom", "{isso não é json");
    CHECK(!loadLoomAny("/tmp/t4c.loom", doc, issues, error, migrated), "JSON inválido rejeitado");

    // referência inexistente na lógica
    writeFile("/tmp/t4d.loom", R"({
      "format": "loom", "version": 1, "id": "x", "name": "X",
      "objects": [], "scripts": [],
      "logic": [{"id": "l", "event": "onTouch", "objectId": "fantasma", "action": "kill"}]
    })");
    CHECK(loadLoomAny("/tmp/t4d.loom", doc, issues, error, migrated), "carrega com problema de referência");
    bool hasRefError = false;
    for (const LoomIssue& issue : issues) if (issue.error && issue.message.find("fantasma") != std::string::npos) hasRefError = true;
    CHECK(hasRefError, "referência inexistente reportada");
  }

  std::printf("\n%s\n", failures == 0 ? "TODOS OS TESTES PASSARAM" : "FALHAS!");
  return failures == 0 ? 0 : 1;
}

// Teste de regressão da auditoria: round-trip completo do .loom v1,
// incluindo scripts, lógica e estado do editor (hidden/locked/group).
#include "../src/loom_format.h"

#include <cstdio>
#include <string>

using namespace loomia;

static int failures = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (cond)                                                                  \
      std::printf("  ok: %s\n", msg);                                          \
    else {                                                                     \
      std::printf("  FALHOU: %s\n", msg);                                      \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

int main() {
  std::printf("== round-trip com scripts, lógica e estado do editor ==\n");

  LoomDocument doc;
  doc.id = "auditoria";
  doc.name = "Auditoria";
  doc.objects.resize(3);
  doc.objects[0].id = "chao";
  doc.objects[0].size = {20, 1, 20};
  doc.objects[0].material = "grass";
  doc.objects[0].editorHidden = false;
  doc.objects[0].editorLocked = true;
  doc.objects[0].editorGroup = "estrutura";

  doc.objects[1].id = "bloco-oculto";
  doc.objects[1].position = {2, 1, 0};
  doc.objects[1].editorHidden = true;
  doc.objects[1].editorGroup = "decoracao";

  doc.objects[2].id = "luz";
  doc.objects[2].editorGroup = "luzes";

  LoomScript script;
  script.id = "boost";
  script.target = "server";
  script.source =
      "function onTouch(part) part.velocity = Vector3.new(0, 50, 0) end";
  doc.scripts.push_back(script);

  LoomLogicEvent logic;
  logic.id = "ev1";
  logic.event = "onTouch";
  logic.objectId = "chao";
  logic.action = "teleport";
  doc.logic.push_back(logic);

  CHECK(validateLoom(doc).empty(), "documento original válido sem erros");

  std::string error;
  CHECK(saveLoomV1("/tmp/auditoria.loom", doc, error), "salva v1");

  LoomDocument loaded;
  std::vector<LoomIssue> issues;
  CHECK(loadLoom("/tmp/auditoria.loom", loaded, issues, error),
        "carrega de volta");

  CHECK(loaded.objects.size() == 3, "3 objetos preservados");
  CHECK(loaded.objects[0].editorLocked, "locked preservado no objeto 0");
  CHECK(loaded.objects[0].editorGroup == "estrutura",
        "grupo preservado no objeto 0");
  CHECK(loaded.objects[1].editorHidden, "hidden preservado no objeto 1");
  CHECK(loaded.objects[2].editorGroup == "luzes",
        "grupo preservado no objeto 2");
  CHECK(loaded.scripts.size() == 1 && loaded.scripts[0].id == "boost",
        "script preservado com id");
  CHECK(loaded.scripts[0].source.find("onTouch") != std::string::npos,
        "código do script preservado");
  CHECK(loaded.logic.size() == 1 && loaded.logic[0].objectId == "chao",
        "lógica preservada");

  // Round-trip duplo (salvar o carregado e comparar de novo)
  CHECK(saveLoomV1("/tmp/auditoria2.loom", loaded, error), "salva novamente");
  LoomDocument again;
  std::vector<LoomIssue> issues2;
  CHECK(loadLoom("/tmp/auditoria2.loom", again, issues2, error),
        "carrega novamente");
  CHECK(again.objects.size() == loaded.objects.size(),
        "objetos estáveis no 2º round-trip");
  CHECK(again.objects[1].editorHidden && again.objects[0].editorLocked,
        "estado do editor estável");
  CHECK(again.logic.size() == 1 && again.scripts.size() == 1,
        "lógica/scripts estáveis");

  std::printf("\n%s\n", failures == 0 ? "AUDITORIA: TODOS OS TESTES PASSARAM"
                                      : "AUDITORIA: FALHAS!");
  return failures == 0 ? 0 : 1;
}

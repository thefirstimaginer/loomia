// Implementação do formato .loom v1 (JSON versionado).
//
// Unidades e convenções (ver docs/loom-format.md):
//   • posições/tamanhos em metros; tamanho = extensão total da caixa
//   • rotação em graus, ordem ZYX intrínseca (yaw, pitch, roll)
//   • cores RGB 0–255 (inteiro)
//   • massa em kg (>0); anchored/collision booleanos (defaults: true/true)
//   • defaults: material "plastic", shape "box", type "part", mass 1
#include "loom_format.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <set>

#include "json.h"

namespace loomia {

namespace {

bool isFiniteVec(const json::ValuePtr& value, int components) {
  if (!value || !value->isArray() || static_cast<int>(value->array.size()) != components) return false;
  for (int index = 0; index < components; ++index) {
    if (!std::isfinite(value->at(index, 0))) return false;
  }
  return true;
}

Vec3 readVec(const json::ValuePtr& value, const Vec3& fallback) {
  if (!isFiniteVec(value, 3)) return fallback;
  return Vec3{static_cast<float>(value->at(0, fallback.x)), static_cast<float>(value->at(1, fallback.y)),
              static_cast<float>(value->at(2, fallback.z))};
}

Color3 readColor(const json::ValuePtr& value, const Color3& fallback) {
  if (!isFiniteVec(value, 3)) return fallback;
  Color3 out;
  out.r = static_cast<uint8_t>(clampf(static_cast<float>(value->at(0, fallback.r)), 0.f, 255.f));
  out.g = static_cast<uint8_t>(clampf(static_cast<float>(value->at(1, fallback.g)), 0.f, 255.f));
  out.b = static_cast<uint8_t>(clampf(static_cast<float>(value->at(2, fallback.b)), 0.f, 255.f));
  return out;
}

json::ValuePtr vec3Json(float x, float y, float z) { return json::makeVec3(x, y, z); }

// Ids estáveis: gerados do índice quando ausentes; duplicatas são renomeadas.
std::string stableObjectId(const std::string& provided, std::set<std::string>& used, int index) {
  std::string candidate = provided;
  if (candidate.empty()) candidate = "obj" + std::to_string(index + 1);
  while (used.count(candidate)) candidate += "_";
  used.insert(candidate);
  return candidate;
}

bool validShape(const std::string& shape) {
  return shape == "box" || shape == "sphere" || shape == "cylinder";
}

bool validEvent(const std::string& event) {
  // Eventos suportados pelo modelo de lógica do jogo.
  return event == "onTouch" || event == "onEnter" || event == "onExit" || event == "onInteract" ||
         event == "onTimer" || event == "onSpawn";
}

bool validAction(const std::string& action) {
  return action == "none" || action == "teleport" || action == "setVel" || action == "kill" ||
         action == "color" || action == "show" || action == "hide" || action == "playSound";
}

}  // namespace

std::vector<LoomIssue> validateLoom(const LoomDocument& doc) {
  std::vector<LoomIssue> issues;
  std::set<std::string> ids;

  if (doc.id.empty()) issues.push_back({true, "campo 'id' do mapa é obrigatório"});
  if (doc.name.empty()) issues.push_back({false, "mapa sem 'name' — será exibido como o id"});

  for (const LoomObject& object : doc.objects) {
    if (!object.id.empty() && !ids.insert(object.id).second) {
      issues.push_back({true, "id de objeto duplicado: '" + object.id + "'"});
    }
    if (!validShape(object.shape)) {
      issues.push_back({true, "objeto '" + object.id + "': shape não suportado '" + object.shape + "'"});
    }
    if (object.size.x <= 0 || object.size.y <= 0 || object.size.z <= 0) {
      issues.push_back({true, "objeto '" + object.id + "': tamanho deve ser positivo"});
    }
    if (!std::isfinite(object.mass) || object.mass < 0) {
      issues.push_back({true, "objeto '" + object.id + "': massa inválida (deve ser >= 0)"});
    }
    if (materialFromName(object.material) == Material::Plastic && object.material != "plastic") {
      issues.push_back({false, "objeto '" + object.id + "': material desconhecido '" + object.material +
                                   "' (será 'plastic')"});
    }
  }

  std::set<std::string> scriptIds;
  for (const LoomScript& script : doc.scripts) {
    if (script.id.empty()) {
      issues.push_back({true, "script sem 'id'"});
    } else if (!scriptIds.insert(script.id).second) {
      issues.push_back({true, "id de script duplicado: '" + script.id + "'"});
    }
    if (script.source.empty() && script.file.empty()) {
      issues.push_back({true, "script '" + script.id + "' sem source e sem file"});
    }
    if (script.language != "lua") {
      issues.push_back({true, "script '" + script.id + "': linguagem não suportada '" + script.language + "'"});
    }
  }

  for (const LoomLogicEvent& entry : doc.logic) {
    if (!validEvent(entry.event)) {
      issues.push_back({true, "lógica '" + entry.id + "': evento não suportado '" + entry.event + "'"});
    }
    if (!validAction(entry.action)) {
      issues.push_back({true, "lógica '" + entry.id + "': ação não suportada '" + entry.action + "'"});
    }
    if (!entry.objectId.empty() && !ids.count(entry.objectId)) {
      issues.push_back({true, "lógica '" + entry.id + "': objeto '" + entry.objectId + "' não existe"});
    }
    if (!entry.targetId.empty() && !ids.count(entry.targetId)) {
      issues.push_back({true, "lógica '" + entry.id + "': alvo '" + entry.targetId + "' não existe"});
    }
  }
  return issues;
}

LoomDocument worldToLoom(const World& world) {
  LoomDocument doc;
  doc.version = 1;
  doc.id = world.id;
  doc.name = world.name;
  doc.description = world.description;
  doc.spawn = world.spawn;
  doc.sky = world.sky;

  std::set<std::string> used;
  int index = 0;
  for (const Block& block : world.blocks) {
    LoomObject object;
    object.id = stableObjectId("", used, index++);
    object.shape = "box";
    object.position = block.position;
    object.size = block.size;
    object.color = block.color;
    object.material = materialName(block.material);
    object.anchored = true;
    object.collision = block.collidable;
    doc.objects.push_back(object);
  }
  for (const Item& item : world.items) {
    LoomObject object;
    object.id = stableObjectId("item_" + item.type, used, index++);
    object.type = "item";
    object.shape = "sphere";
    object.position = item.position;
    object.size = {item.scale, item.scale, item.scale};
    object.collision = false;
    doc.objects.push_back(object);
  }
  for (const Npc& npc : world.npcs) {
    LoomObject object;
    object.id = stableObjectId("npc_" + npc.name, used, index++);
    object.type = "npc";
    object.shape = "box";
    object.position = npc.position;
    doc.objects.push_back(object);
  }

  // Scripts legados (objeto scripts: local/global/action/physics) viram entradas.
  const auto addLegacy = [&](const std::string& key, const std::string& code) {
    if (code.empty()) return;
    LoomScript script;
    script.id = "legacy_" + key;
    script.source = code;
    doc.scripts.push_back(script);
  };
  addLegacy("local", world.scripts.local);
  addLegacy("global", world.scripts.global);
  addLegacy("action", world.scripts.action);
  addLegacy("physics", world.scripts.physics);
  return doc;
}

World loomToWorld(const LoomDocument& doc) {
  World world;
  world.id = doc.id;
  world.name = doc.name;
  world.description = doc.description;
  world.spawn = doc.spawn;
  world.sky = doc.sky;
  world.version = doc.version;

  for (const LoomObject& object : doc.objects) {
    if (object.type == "npc") {
      Npc npc;
      npc.name = object.id;
      npc.position = object.position;
      world.npcs.push_back(npc);
      continue;
    }
    if (object.type == "item") {
      Item item;
      item.type = object.id.substr(object.id.find('_') + 1);
      item.position = object.position;
      item.scale = object.size.x;
      world.items.push_back(item);
      continue;
    }
    Block block;
    block.position = object.position;
    block.size = object.size;
    block.color = object.color;
    block.material = materialFromName(object.material);
    block.collidable = object.collision;
    world.blocks.push_back(block);
  }

  // Scripts estruturados -> campos legados do World (o cliente hoje lê esses).
  for (const LoomScript& script : doc.scripts) {
    if (script.target == "client" || script.target == "both") {
      if (script.id == "legacy_local") world.scripts.local = script.source;
      else if (script.id == "legacy_global") world.scripts.global = script.source;
      else if (script.id == "legacy_action") world.scripts.action = script.source;
      else if (script.id == "legacy_physics") world.scripts.physics = script.source;
    }
  }
  return world;
}

std::string serializeLoom(const LoomDocument& doc) {
  auto root = json::makeObject();
  json::set(*root, "format", json::makeString("loom"));
  json::set(*root, "version", json::makeNumber(doc.version));
  json::set(*root, "id", json::makeString(doc.id));
  json::set(*root, "name", json::makeString(doc.name));
  if (!doc.description.empty()) json::set(*root, "description", json::makeString(doc.description));

  auto settings = json::makeObject();
  json::set(*settings, "spawn", vec3Json(doc.spawn.x, doc.spawn.y, doc.spawn.z));
  json::set(*settings, "skyColor", json::makeVec3(doc.sky.r, doc.sky.g, doc.sky.b));
  json::set(*root, "settings", settings);

  auto objects = json::makeArray();
  for (const LoomObject& object : doc.objects) {
    auto entry = json::makeObject();
    json::set(*entry, "id", json::makeString(object.id));
    json::set(*entry, "type", json::makeString(object.type));
    json::set(*entry, "shape", json::makeString(object.shape));
    auto transform = json::makeObject();
    json::set(*transform, "position", vec3Json(object.position.x, object.position.y, object.position.z));
    json::set(*transform, "rotation", vec3Json(object.rotation.x, object.rotation.y, object.rotation.z));
    json::set(*transform, "size", vec3Json(object.size.x, object.size.y, object.size.z));
    json::set(*entry, "transform", transform);
    auto appearance = json::makeObject();
    json::set(*appearance, "color", json::makeVec3(object.color.r, object.color.g, object.color.b));
    json::set(*appearance, "material", json::makeString(object.material));
    json::set(*entry, "appearance", appearance);
    auto physics = json::makeObject();
    json::set(*physics, "anchored", json::makeBool(object.anchored));
    json::set(*physics, "collision", json::makeBool(object.collision));
    json::set(*physics, "mass", json::makeNumber(object.mass));
    json::set(*entry, "physics", physics);
    json::push(*objects, entry);
  }
  json::set(*root, "objects", objects);

  auto scripts = json::makeArray();
  for (const LoomScript& script : doc.scripts) {
    auto entry = json::makeObject();
    json::set(*entry, "id", json::makeString(script.id));
    json::set(*entry, "language", json::makeString(script.language));
    json::set(*entry, "target", json::makeString(script.target));
    if (!script.file.empty()) {
      json::set(*entry, "file", json::makeString(script.file));
    }
    if (!script.source.empty()) {
      json::set(*entry, "source", json::makeString(script.source));
    }
    json::push(*scripts, entry);
  }
  json::set(*root, "scripts", scripts);

  auto logic = json::makeArray();
  for (const LoomLogicEvent& entry : doc.logic) {
    auto node = json::makeObject();
    json::set(*node, "id", json::makeString(entry.id));
    json::set(*node, "event", json::makeString(entry.event));
    json::set(*node, "objectId", json::makeString(entry.objectId));
    json::set(*node, "action", json::makeString(entry.action));
    if (!entry.targetId.empty()) json::set(*node, "targetId", json::makeString(entry.targetId));
    if (entry.value != 0) json::set(*node, "value", json::makeNumber(entry.value));
    json::push(*logic, node);
  }
  json::set(*root, "logic", logic);
  return json::write(*root, true);
}

bool loadLoom(const std::string& path, LoomDocument& doc, std::vector<LoomIssue>& issues, std::string& error) {
  std::ifstream file(path);
  if (!file) {
    error = "não consegui abrir " + path;
    return false;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  const std::string text = buffer.str();

  const json::ValuePtr root = json::parse(text, &error);
  if (!root || !root->isObject()) {
    if (error.empty()) error = "o arquivo precisa ser um objeto JSON";
    return false;
  }

  // Compatibilidade de versão.
  const std::string format = root->str("format", "loom");
  if (format != "loom") {
    error = "formato não suportado: '" + format + "' (esperado 'loom')";
    return false;
  }
  const double version = root->num("version", 0);
  if (version != 1) {
    error = "versão " + std::to_string(version) + " não suportada — este arquivo precisa de uma versão mais nova do Loomia";
    return false;
  }

  doc = LoomDocument{};
  doc.version = 1;
  doc.id = root->str("id", "mapa");
  doc.name = root->str("name", doc.id);
  doc.description = root->str("description", "");
  if (const json::ValuePtr settings = root->get("settings")) {
    doc.spawn = readVec(settings->get("spawn"), Vec3{0, 1.5f, 6});
    doc.sky = readColor(settings->get("skyColor"), Color3{150, 200, 240});
  } else {
    // tolerância: settings no topo (formatos salvos à mão)
    doc.spawn = readVec(root->get("spawn"), Vec3{0, 1.5f, 6});
    doc.sky = readColor(root->get("skyColor"), Color3{150, 200, 240});
  }

  std::set<std::string> usedIds;
  if (const json::ValuePtr objects = root->get("objects")) {
    if (!objects->isArray()) {
      error = "'objects' precisa ser uma lista";
      return false;
    }
    int index = 0;
    for (const json::ValuePtr& entry : objects->array) {
      if (!entry || !entry->isObject()) continue;
      LoomObject object;
      object.id = stableObjectId(entry->str("id", ""), usedIds, index++);
      object.type = entry->str("type", "part");
      object.shape = entry->str("shape", "box");
      if (const json::ValuePtr transform = entry->get("transform")) {
        object.position = readVec(transform->get("position"), object.position);
        object.rotation = readVec(transform->get("rotation"), Vec3{0, 0, 0});
        object.size = readVec(transform->get("size"), object.size);
      } else {
        object.position = readVec(entry->get("position"), object.position);
        object.size = readVec(entry->get("size"), object.size);
      }
      if (const json::ValuePtr appearance = entry->get("appearance")) {
        object.color = readColor(appearance->get("color"), object.color);
        object.material = appearance->str("material", "plastic");
      }
      if (const json::ValuePtr physics = entry->get("physics")) {
        object.anchored = physics->flag("anchored", true);
        object.collision = physics->flag("collision", true);
        object.mass = static_cast<float>(physics->num("mass", 1.0));
      }
      doc.objects.push_back(object);
    }
  }

  if (const json::ValuePtr scripts = root->get("scripts")) {
    if (scripts->isArray()) {
      for (const json::ValuePtr& entry : scripts->array) {
        if (!entry || !entry->isObject()) continue;
        LoomScript script;
        script.id = entry->str("id", "");
        script.language = entry->str("language", "lua");
        script.target = entry->str("target", "server");
        script.file = entry->str("file", "");
        script.source = entry->str("source", "");
        doc.scripts.push_back(script);
      }
    }
  }

  if (const json::ValuePtr logic = root->get("logic")) {
    if (logic->isArray()) {
      for (const json::ValuePtr& entry : logic->array) {
        if (!entry || !entry->isObject()) continue;
        LoomLogicEvent node;
        node.id = entry->str("id", "");
        node.event = entry->str("event", "onTouch");
        node.objectId = entry->str("objectId", "");
        node.action = entry->str("action", "none");
        node.targetId = entry->str("targetId", "");
        node.value = entry->num("value", 0);
        doc.logic.push_back(node);
      }
    }
  }

  issues = validateLoom(doc);
  return true;
}

bool saveLoomV1(const std::string& path, const LoomDocument& doc, std::string& error) {
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    error = "não consegui escrever " + path;
    return false;
  }
  file << serializeLoom(doc);
  file.flush();
  if (!file) {
    error = "falha ao gravar " + path;
    return false;
  }
  return true;
}

bool loadLoomAny(const std::string& path, LoomDocument& doc, std::vector<LoomIssue>& issues, std::string& error,
                 bool& migrated) {
  migrated = false;
  // Tenta v1 primeiro.
  if (loadLoom(path, doc, issues, error)) return true;

  // Detecta formato legado (tem "blocks" e não tem "objects").
  std::ifstream file(path);
  std::stringstream buffer;
  buffer << file.rdbuf();
  const std::string text = buffer.str();
  if (text.find("\"blocks\"") == std::string::npos) return false;  // erro real do v1

  World legacy;
  std::string legacyError;
  if (!loadWorld(path, legacy, legacyError)) {
    error = legacyError;
    return false;
  }
  doc = worldToLoom(legacy);
  issues = validateLoom(doc);
  migrated = true;
  return true;
}

}  // namespace loomia

#include "world.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "json.h"

namespace loomia {

const char* materialName(Material material) {
  switch (material) {
    case Material::Grass: return "grass";
    case Material::Wood: return "wood";
    case Material::Brick: return "brick";
    case Material::Stone: return "stone";
    case Material::Neon: return "neon";
    case Material::Plastic:
    default: return "plastic";
  }
}

Material materialFromName(const std::string& name) {
  if (name == "grass") return Material::Grass;
  if (name == "wood") return Material::Wood;
  if (name == "brick") return Material::Brick;
  if (name == "stone") return Material::Stone;
  if (name == "neon") return Material::Neon;
  return Material::Plastic;
}

void World::bounds(const Block& block, Vec3& min, Vec3& max) {
  const Vec3 half{block.size.x * 0.5f, block.size.y * 0.5f, block.size.z * 0.5f};
  min = block.position - half;
  max = block.position + half;
}

int World::blockAt(Vec3 point) const {
  // Do último para o primeiro: o bloco mais recente "ganha" quando há
  // sobreposição (comportamento esperado no editor).
  for (int index = static_cast<int>(blocks.size()) - 1; index >= 0; index--) {
    Vec3 min, max;
    bounds(blocks[static_cast<size_t>(index)], min, max);
    if (point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y &&
        point.z >= min.z && point.z <= max.z) {
      return index;
    }
  }
  return -1;
}

namespace {

Color3 readColor(const json::ValuePtr& value, const Color3& fallback) {
  if (!value || !value->isArray() || value->array.size() < 3) return fallback;
  Color3 color;
  color.r = static_cast<uint8_t>(clampf(static_cast<float>(value->at(0, fallback.r)), 0, 255));
  color.g = static_cast<uint8_t>(clampf(static_cast<float>(value->at(1, fallback.g)), 0, 255));
  color.b = static_cast<uint8_t>(clampf(static_cast<float>(value->at(2, fallback.b)), 0, 255));
  return color;
}

Vec3 readVec3(const json::ValuePtr& value, const Vec3& fallback) {
  double x, y, z;
  if (!json::readVec3(value, x, y, z)) return fallback;
  return Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

json::ValuePtr writeColor(const Color3& color) {
  return json::makeVec3(color.r, color.g, color.b);
}

bool parseInto(const std::string& text, World& world, std::string& error) {
  const json::ValuePtr root = json::parse(text, &error);
  if (!root) return false;
  if (!root->isObject()) {
    error = "o mapa precisa ser um objeto JSON";
    return false;
  }
  if (!root->get("blocks") || !root->get("blocks")->isArray()) {
    error = "o mapa não tem a lista 'blocks'";
    return false;
  }

  World parsed;
  parsed.id = root->str("id", "mapa");
  parsed.name = root->str("name", parsed.id);
  parsed.description = root->str("description", "");
  parsed.spawn = readVec3(root->get("spawn"), Vec3{0, 2, 0});
  parsed.sky = readColor(root->get("sky_color"), Color3{150, 200, 240});
  parsed.version = static_cast<int>(root->num("version", 1));

  const json::ValuePtr blocks = root->get("blocks");
  parsed.blocks.reserve(blocks->array.size());
  for (const json::ValuePtr& entry : blocks->array) {
    if (!entry || !entry->isObject()) continue;
    Block block;
    block.position = readVec3(entry->get("position"), Vec3{0, 0, 0});
    block.size = readVec3(entry->get("size"), Vec3{1, 1, 1});
    block.color = readColor(entry->get("color"), Color3{200, 200, 200});
    block.material = materialFromName(entry->str("material", "plastic"));
    block.collidable = entry->flag("collidable", true);
    // Tamanho mínimo evita blocos "invisíveis" quebram a física.
    block.size.x = std::max(0.05f, block.size.x);
    block.size.y = std::max(0.05f, block.size.y);
    block.size.z = std::max(0.05f, block.size.z);
    parsed.blocks.push_back(block);
  }

  if (const json::ValuePtr items = root->get("items")) {
    if (items->isArray()) {
      for (const json::ValuePtr& entry : items->array) {
        if (!entry || !entry->isObject()) continue;
        Item item;
        item.type = entry->str("type", "coin");
        item.position = readVec3(entry->get("position"), Vec3{0, 0, 0});
        item.scale = static_cast<float>(entry->num("scale", 1.0));
        parsed.items.push_back(item);
      }
    }
  }

  if (const json::ValuePtr npcs = root->get("npcs")) {
    if (npcs->isArray()) {
      for (const json::ValuePtr& entry : npcs->array) {
        if (!entry || !entry->isObject()) continue;
        Npc npc;
        npc.name = entry->str("name", "NPC");
        npc.position = readVec3(entry->get("position"), Vec3{0, 0, 0});
        npc.script = entry->str("script", "");
        parsed.npcs.push_back(npc);
      }
    }
  }

  if (const json::ValuePtr scripts = root->get("scripts")) {
    if (scripts->isObject()) {
      parsed.scripts.local = scripts->str("local", "");
      parsed.scripts.global = scripts->str("global", "");
      parsed.scripts.action = scripts->str("action", "");
      parsed.scripts.physics = scripts->str("physics", "");
    }
  }

  world = std::move(parsed);
  return true;
}

}  // namespace

bool parseWorld(const std::string& text, World& world, std::string& error) {
  return parseInto(text, world, error);
}

bool loadWorld(const std::string& path, World& world, std::string& error) {
  std::ifstream file(path);
  if (!file) {
    error = "não consegui abrir " + path;
    return false;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return parseInto(buffer.str(), world, error);
}

std::string serializeWorld(const World& world) {
  auto root = json::makeObject();
  json::set(*root, "id", json::makeString(world.id));
  json::set(*root, "name", json::makeString(world.name));
  if (!world.description.empty()) {
    json::set(*root, "description", json::makeString(world.description));
  }
  json::set(*root, "spawn", json::makeVec3(world.spawn.x, world.spawn.y, world.spawn.z));
  json::set(*root, "sky_color", writeColor(world.sky));

  auto blocks = json::makeArray();
  for (const Block& block : world.blocks) {
    auto entry = json::makeObject();
    json::set(*entry, "position", json::makeVec3(block.position.x, block.position.y, block.position.z));
    json::set(*entry, "size", json::makeVec3(block.size.x, block.size.y, block.size.z));
    json::set(*entry, "color", writeColor(block.color));
    json::set(*entry, "material", json::makeString(materialName(block.material)));
    if (!block.collidable) json::set(*entry, "collidable", json::makeBool(false));
    json::push(*blocks, entry);
  }
  json::set(*root, "blocks", blocks);

  if (!world.items.empty()) {
    auto items = json::makeArray();
    for (const Item& item : world.items) {
      auto entry = json::makeObject();
      json::set(*entry, "type", json::makeString(item.type));
      json::set(*entry, "position", json::makeVec3(item.position.x, item.position.y, item.position.z));
      if (std::fabs(item.scale - 1.0f) > 0.001f) {
        json::set(*entry, "scale", json::makeNumber(item.scale));
      }
      json::push(*items, entry);
    }
    json::set(*root, "items", items);
  }

  if (!world.npcs.empty()) {
    auto npcs = json::makeArray();
    for (const Npc& npc : world.npcs) {
      auto entry = json::makeObject();
      json::set(*entry, "name", json::makeString(npc.name));
      json::set(*entry, "position", json::makeVec3(npc.position.x, npc.position.y, npc.position.z));
      if (!npc.script.empty()) json::set(*entry, "script", json::makeString(npc.script));
      json::push(*npcs, entry);
    }
    json::set(*root, "npcs", npcs);
  }

  const bool hasScripts = !world.scripts.local.empty() || !world.scripts.global.empty() ||
                          !world.scripts.action.empty() || !world.scripts.physics.empty();
  if (hasScripts) {
    auto scripts = json::makeObject();
    if (!world.scripts.local.empty()) json::set(*scripts, "local", json::makeString(world.scripts.local));
    if (!world.scripts.global.empty()) json::set(*scripts, "global", json::makeString(world.scripts.global));
    if (!world.scripts.action.empty()) json::set(*scripts, "action", json::makeString(world.scripts.action));
    if (!world.scripts.physics.empty()) json::set(*scripts, "physics", json::makeString(world.scripts.physics));
    json::set(*root, "scripts", scripts);
  }

  json::set(*root, "version", json::makeNumber(world.version));
  return json::write(*root, true);
}

bool saveWorld(const std::string& path, const World& world, std::string& error) {
  std::ofstream file(path, std::ios::trunc);
  if (!file) {
    error = "não consegui escrever " + path;
    return false;
  }
  file << serializeWorld(world);
  file.flush();
  if (!file) {
    error = "falha ao gravar " + path;
    return false;
  }
  return true;
}

}  // namespace loomia

// Hierarquia avançada: busca, ocultar/mostrar, bloquear, grupos + inserção
// de primitivas (box/sphere/cylinder) e luz.
//
// Os estados hidden/locked/grupo ficam em vetores paralelos alinhados aos
// blocos do mundo (o formato .loom v1 os preserva via worldToLoom quando
// integrado; por ora são do editor).
#include "editor.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cmath>

namespace loomia {

// Garante que os vetores paralelos têm exatamente o tamanho do mundo.
void EditorApp::syncBlockMeta() {
  const size_t count = world_.blocks.size();
  blockHidden_.resize(count, false);
  blockLocked_.resize(count, false);
  blockGroups_.resize(count);
}

// Remove a entrada de metadados de um bloco (mantém o alinhamento).
void EditorApp::eraseBlockMeta(int index) {
  if (index < 0) return;
  if (static_cast<size_t>(index) < blockHidden_.size()) blockHidden_.erase(blockHidden_.begin() + index);
  if (static_cast<size_t>(index) < blockLocked_.size()) blockLocked_.erase(blockLocked_.begin() + index);
  if (static_cast<size_t>(index) < blockGroups_.size()) blockGroups_.erase(blockGroups_.begin() + index);
}

void EditorApp::toggleHidden(int index) {
  if (index < 0 || index >= static_cast<int>(world_.blocks.size())) return;
  if (blockHidden_.size() != world_.blocks.size()) blockHidden_.resize(world_.blocks.size(), false);
  blockHidden_[static_cast<size_t>(index)] = !blockHidden_[static_cast<size_t>(index)];
  setStatus(blockHidden_[static_cast<size_t>(index)] ? "Objeto oculto." : "Objeto visível.");
}

void EditorApp::toggleLocked(int index) {
  if (index < 0 || index >= static_cast<int>(world_.blocks.size())) return;
  if (blockLocked_.size() != world_.blocks.size()) blockLocked_.resize(world_.blocks.size(), false);
  blockLocked_[static_cast<size_t>(index)] = !blockLocked_[static_cast<size_t>(index)];
  setStatus(blockLocked_[static_cast<size_t>(index)] ? "Objeto bloqueado (não selecionável)."
                                                     : "Objeto desbloqueado.");
}

void EditorApp::setGroup(int index, const std::string& group) {
  if (index < 0 || index >= static_cast<int>(world_.blocks.size())) return;
  if (blockGroups_.size() != world_.blocks.size()) blockGroups_.resize(world_.blocks.size());
  blockGroups_[static_cast<size_t>(index)] = group;
  markDirty();
}

// Insere uma primitiva nova no centro da câmera, seleciona e dá undo.
void EditorApp::addObjectPrimitive(int shapeKind) {
  pushUndo();
  Block block;
  block.position = camTarget_ + Vec3{0, 1.5f, 0};
  block.color = {static_cast<float>(currentColor_[0]), static_cast<float>(currentColor_[1]),
                 static_cast<float>(currentColor_[2])};
  block.material = static_cast<Material>(currentMaterial_);
  switch (shapeKind) {
    case 1:  // esfera: caixa cúbica (o cliente desenha caixa)
      block.size = {2, 2, 2};
      break;
    case 2:  // cilindro: caixa mais alta e fina
      block.size = {1.4f, 3.f, 1.4f};
      break;
    default:  // caixa
      block.size = {2, 1, 2};
      break;
  }
  world_.blocks.push_back(block);
  if (blockHidden_.size() != world_.blocks.size()) blockHidden_.resize(world_.blocks.size(), false);
  if (blockLocked_.size() != world_.blocks.size()) blockLocked_.resize(world_.blocks.size(), false);
  if (blockGroups_.size() != world_.blocks.size()) blockGroups_.resize(world_.blocks.size());
  blockGroups_.back() = shapeKind == 1 ? "esferas" : shapeKind == 2 ? "cilindros" : "caixas";
  selected_ = static_cast<int>(world_.blocks.size()) - 1;
  selection_.clear();
  selection_.push_back(selected_);
  renderer_.upload(world_);
  markDirty();
  const char* names[] = {"caixa", "esfera", "cilindro"};
  setStatus(std::string("Primitiva inserida: ") + names[shapeKind]);
}

// Luz: um bloco neon pequeno não colidível (representação visual da luz).
void EditorApp::addLight() {
  pushUndo();
  Block block;
  block.position = camTarget_ + Vec3{0, 4, 0};
  block.size = {0.5f, 0.5f, 0.5f};
  block.color = {255, 245, 180};
  block.material = Material::Neon;
  block.collidable = false;
  world_.blocks.push_back(block);
  if (blockHidden_.size() != world_.blocks.size()) blockHidden_.resize(world_.blocks.size(), false);
  if (blockLocked_.size() != world_.blocks.size()) blockLocked_.resize(world_.blocks.size(), false);
  if (blockGroups_.size() != world_.blocks.size()) blockGroups_.resize(world_.blocks.size());
  blockGroups_.back() = "luzes";
  selected_ = static_cast<int>(world_.blocks.size()) - 1;
  selection_.clear();
  selection_.push_back(selected_);
  renderer_.upload(world_);
  markDirty();
  setStatus("Luz inserida (bloco neon não colidível).");
}

}  // namespace loomia

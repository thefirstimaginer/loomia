// Ações de arquivo e edição: templates, abrir/salvar .loom, recentes,
// cópia, renomear, seleção (simples/múltipla), undo/redo, foco e snap.
#include "editor.h"

#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <fstream>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace loomia {
namespace {
std::string recentsPath() {
  const char* home = std::getenv("HOME");
  return (home ? std::string(home) : std::string("/tmp")) + "/.loomia-studio-recentes.txt";
}
}  // namespace
void EditorApp::newFromTemplate(int templateIndex) {
  const TemplateId ids[] = {TemplateId::Baseplate, TemplateId::SkyIslands, TemplateId::Obby, TemplateId::Showcase};
  world_ = makeTemplate(ids[std::clamp(templateIndex, 0, 3)]);
  filePath_.clear();
  dirty_ = false;
  selected_ = -1;
  selection_.clear();
  undoStack_.clear();
  redoStack_.clear();
  renderer_.upload(world_);
  inEditor_ = true;
  setStatus("Novo mapa criado. Ctrl+S salva, Ctrl+Shift+S salva como...");
}

bool EditorApp::openLoom(const std::string& path, std::string& error) {
  // Formato v1 (ou legado migrado). Avisos de validação aparecem no status.
  std::vector<LoomIssue> issues;
  bool migrated = false;
  LoomDocument doc;
  if (!loadLoomAny(path, doc, issues, error, migrated)) return false;
  int errors = 0;
  for (const LoomIssue& issue : issues) {
    if (issue.error) ++errors;
  }
  if (errors > 0) {
    setStatus(std::to_string(errors) + " problema(s) de validação em " + path);
  }
  if (migrated) setStatus("Formato antigo migrado para .loom v1.");
  world_ = loomToWorld(doc);
  loomDoc_ = doc;
  filePath_ = path;
  dirty_ = false;
  selected_ = -1;
  selection_.clear();
  undoStack_.clear();
  redoStack_.clear();
  renderer_.upload(world_);
  inEditor_ = true;
  setStatus("Mapa carregado: " + path);
  return true;
}

bool EditorApp::saveLoom(const std::string& path, std::string& error) {
  if (!saveWorld(path, world_, error)) return false;
  filePath_ = path;
  dirty_ = false;
  setStatus("Salvo em " + path);
  return true;
}

// ------------------------------------------------------------------ câmera

void EditorApp::deleteSelection() {
  if (selection_.empty()) {
    if (selected_ < 0 || selected_ >= static_cast<int>(world_.blocks.size())) return;
    selection_.push_back(selected_);
  }
  pushUndo();
  // Remove do maior índice para o menor (índices estáveis).
  std::sort(selection_.rbegin(), selection_.rend());
  for (const int index : selection_) {
    if (index >= 0 && index < static_cast<int>(world_.blocks.size())) {
      world_.blocks.erase(world_.blocks.begin() + index);
    }
  }
  setStatus(std::to_string(selection_.size()) + " bloco(s) removido(s) (Delete).");
  selection_.clear();
  selected_ = -1;
  renderer_.upload(world_);
  markDirty();
}

void EditorApp::duplicateSelection() {
  if (selection_.empty()) {
    if (selected_ < 0 || selected_ >= static_cast<int>(world_.blocks.size())) return;
    selection_.push_back(selected_);
  }
  pushUndo();
  std::vector<int> originals = selection_;  // índices crescentes
  for (const int index : originals) {
    if (index < 0 || index >= static_cast<int>(world_.blocks.size())) continue;
    Block copy = world_.blocks[static_cast<size_t>(index)];
    copy.position.z += copy.size.z;  // encosta na frente do original
    world_.blocks.push_back(copy);
  }
  // A seleção passa a apontar para as cópias (no fim do vetor).
  const int total = static_cast<int>(world_.blocks.size());
  selection_.clear();
  for (size_t step = 0; step < originals.size(); ++step) {
    selection_.push_back(total - static_cast<int>(originals.size()) + static_cast<int>(step));
  }
  selected_ = selection_.back();
  renderer_.upload(world_);
  markDirty();
  setStatus(std::to_string(originals.size()) + " bloco(s) duplicado(s) (Ctrl+D).");
}

void EditorApp::focusSelection() {
  if (selected_ < 0 || selected_ >= static_cast<int>(world_.blocks.size())) return;
  camTarget_ = world_.blocks[static_cast<size_t>(selected_)].position;
  setStatus("Câmera centralizada no bloco (F).");
}

void EditorApp::snapSelection() {
  std::vector<int> targets = selection_;
  if (targets.empty() && selected_ >= 0) targets.push_back(selected_);
  if (targets.empty()) return;
  pushUndo();
  for (const int index : targets) {
    if (index < 0 || index >= static_cast<int>(world_.blocks.size())) continue;
    Block& block = world_.blocks[static_cast<size_t>(index)];
    block.position.x = std::round(block.position.x);
    block.position.y = std::round(block.position.y);
    block.position.z = std::round(block.position.z);
  }
  renderer_.upload(world_);
  markDirty();
  setStatus("Posições arredondadas (G).");
}

void EditorApp::loadRecents() {
  recentList_.clear();
  std::ifstream file(recentsPath());
  std::string line;
  while (std::getline(file, line) && recentList_.size() < 8) {
    if (!line.empty() && std::filesystem::exists(line)) recentList_.push_back(line);
  }
}

void EditorApp::saveRecents() {
  std::ofstream file(recentsPath());
  for (const std::string& path : recentList_) file << path << '\n';
}

void EditorApp::addRecent(const std::string& path) {
  // Remove duplicado e coloca no topo.
  recentList_.erase(std::remove(recentList_.begin(), recentList_.end(), path), recentList_.end());
  recentList_.insert(recentList_.begin(), path);
  if (recentList_.size() > 8) recentList_.resize(8);
  saveRecents();
}

/** Pede o caminho (zenity) e salva. Devolve false se cancelado. */
bool EditorApp::saveWithDialog() {
  FILE* pipe = popen("zenity --file-selection --save --confirm-overwrite --title='Salvar mapa .loom' 2>/dev/null | tr -d '\\n'", "r");
  char buffer[1024] = {};
  bool ok = false;
  if (pipe && fgets(buffer, sizeof(buffer), pipe)) {
    std::string target = buffer;
    if (!target.empty()) {
      if (target.find(".loom") == std::string::npos) target += ".loom";
      std::string err;
      ok = saveLoom(target, err);
      if (!ok) setStatus("Erro ao salvar: " + err);
      else addRecent(target);
    }
  }
  if (pipe) pclose(pipe);
  return ok;
}

/** Cria uma cópia do mapa em um novo arquivo. */
void EditorApp::saveCopy() {
  const std::string backup = filePath_;
  filePath_.clear();  // força o diálogo perguntar onde
  if (!saveWithDialog()) {
    filePath_ = backup;  // cancelado: mantém o original
    setStatus("Cópia cancelada.");
  }
}

/** Renomeia o mapa (nome exibido) direto pela linha de comando do zenity. */
void EditorApp::renameMap() {
  std::string command = "zenity --entry --title='Renomear mapa' --text='Novo nome:' --entry-text='" + world_.name + "' 2>/dev/null | tr -d '\\n'";
  FILE* pipe = popen(command.c_str(), "r");
  char buffer[256] = {};
  if (pipe && fgets(buffer, sizeof(buffer), pipe)) {
    const std::string newName = buffer;
    if (!newName.empty()) {
      pushUndo();
      world_.name = newName;
      markDirty();
      setStatus("Mapa renomeado para \"" + newName + "\".");
    }
  }
  if (pipe) pclose(pipe);
}

/** Volta para a welcome screen (pergunta antes se há alterações). */
void EditorApp::backToWelcome() {
  if (dirty_) {
    confirmExit_ = true;
    return;
  }
  inEditor_ = false;
  setStatus("Bem-vindo ao Loomia Studio!");
}

/** Modal: "salvar antes de sair/voltar?" — 3 opções. */

}  // namespace loomia

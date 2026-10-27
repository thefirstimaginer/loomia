// Atlas de ícones PNG (misc/icons/loomia-studio-icons) desenhados no overlay.
//
// Cada ícone vira uma textura GL; o draw faz um quad texturizado no batch de
// triângulos da UI (com blend para o alpha do PNG).
#pragma once

#include <string>
#include <unordered_map>

namespace loomia {

class IconAtlas {
 public:
  // Carrega todos os ícones; chama uma vez no init (contexto GL corrente).
  bool init(const std::string& iconsDir, std::string& error);
  void destroy();

  bool ready() const { return !textures_.empty(); }

  // Desenha o ícone no overlay (x,y topo-esquerda, lado quadrado `size`).
  // Precisa ser chamado entre begin/end de blend, com o shader da UI ativo.
  void draw(class UiOverlay& ui, const std::string& name, float x, float y, float size, float alpha = 1.f) const;

  // Nomes disponíveis (sem extensão).
  bool has(const std::string& name) const { return textures_.count(name) != 0; }

 private:
  struct Icon {
    unsigned int texture = 0;
    int width = 0, height = 0;
  };
  std::unordered_map<std::string, Icon> textures_;
  unsigned int shader_ = 0, vao_ = 0, vbo_ = 0;
  int uniformRes_ = -1, uniformSampler_ = -1;
  bool ready_ = false;
};

}  // namespace loomia

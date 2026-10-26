// Templates de mapa para a tela de boas-vindas do studio.
#include "templates.h"

#include <cmath>

namespace loomia {

World makeTemplate(TemplateId id) {
  World world;
  world.spawn = {0, 1.5f, 6};
  world.sky = {150, 200, 240};
  world.version = 1;

  switch (id) {
    case TemplateId::Baseplate: {
      world.id = "baseplate";
      world.name = "Baseplate";
      world.description = "Um piso limpo para começar do zero.";
      world.blocks.push_back({{0, -0.5f, 0}, {48, 1, 48}, {100, 160, 60}, Material::Grass, true});
      break;
    }
    case TemplateId::SkyIslands: {
      world.id = "sky-islands";
      world.name = "Ilhas Flutuantes";
      world.description = "Três ilhas no céu ligadas por pontes.";
      world.sky = {120, 170, 235};
      world.blocks.push_back({{0, -0.5f, 0}, {20, 1, 20}, {100, 160, 60}, Material::Grass, true});
      world.blocks.push_back({{0, 7, -18}, {14, 1, 14}, {100, 160, 60}, Material::Grass, true});
      world.blocks.push_back({{0, 13, -32}, {10, 1, 10}, {100, 160, 60}, Material::Grass, true});
      // Pontes
      world.blocks.push_back({{0, 3.5f, -11}, {2, 0.5f, 8}, {190, 140, 70}, Material::Wood, true});
      world.blocks.push_back({{0, 10.5f, -25}, {2, 0.5f, 8}, {190, 140, 70}, Material::Wood, true});
      // Decoração
      world.blocks.push_back({{5, 1.5f, 3}, {1, 3, 1}, {110, 80, 50}, Material::Wood, true});
      world.blocks.push_back({{6, 4.5f, 3}, {3, 3, 3}, {70, 150, 70}, Material::Plastic, false});
      break;
    }
    case TemplateId::Obby: {
      world.id = "obby";
      world.name = "Obby (Parkour)";
      world.description = "Plataformas suspensas: pule até a bandeira.";
      world.sky = {255, 190, 120};
      world.blocks.push_back({{0, -0.5f, 0}, {14, 1, 10}, {100, 160, 60}, Material::Grass, true});
      for (int index = 0; index < 7; ++index) {
        const float z = -8.0f - index * 4.0f;
        const float y = 0.5f + index * 1.2f;
        const float x = (index % 2 == 0) ? -2.5f : 2.5f;
        world.blocks.push_back({{x, y, z}, {4, 0.6f, 3}, {230, 190, 40}, Material::Plastic, true});
      }
      world.blocks.push_back({{0, 9.4f, -36}, {6, 0.8f, 6}, {200, 60, 60}, Material::Brick, true});
      break;
    }
    case TemplateId::Showcase: {
      world.id = "showcase";
      world.name = "Sala de Exposição";
      world.description = "Um salão com vitrines de blocos de exemplo.";
      world.sky = {40, 44, 60};
      world.blocks.push_back({{0, -0.5f, 0}, {30, 1, 30}, {70, 70, 80}, Material::Stone, true});
      // Paredes
      world.blocks.push_back({{0, 5, -15}, {30, 10, 1}, {230, 230, 235}, Material::Plastic, true});
      world.blocks.push_back({{0, 5, 15}, {30, 10, 1}, {230, 230, 235}, Material::Plastic, true});
      world.blocks.push_back({{-15, 5, 0}, {1, 10, 30}, {230, 230, 235}, Material::Plastic, true});
      world.blocks.push_back({{15, 5, 0}, {1, 10, 30}, {230, 230, 235}, Material::Plastic, true});
      // Vitrines: um bloco de cada material
      const char* names[] = {"plastic", "grass", "wood", "brick", "stone", "neon"};
      for (int index = 0; index < 6; ++index) {
        const float x = -10.0f + index * 4.0f;
        Material material = materialFromName(names[index]);
        world.blocks.push_back({{x, 1.25f, -6}, {2, 1.5f, 2}, {180, 180, 190}, Material::Plastic, true});
        world.blocks.push_back({{x, 3, -6}, {1, 1, 1}, {230, 190, 40}, material, true});
      }
      break;
    }
  }
  return world;
}

}  // namespace loomia

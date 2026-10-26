// Templates de mapa para a tela de boas-vindas do studio.
#pragma once

#include "world.h"

namespace loomia {

enum class TemplateId { Baseplate, SkyIslands, Obby, Showcase };

World makeTemplate(TemplateId id);

}  // namespace loomia

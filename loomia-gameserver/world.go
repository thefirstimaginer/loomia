package main

// Formato do mapa (.loom) e a matemática de colisão.
//
// O `.loom` é JSON puro para o master/editor poderem gerar e validar mapas sem
// nenhuma dependência. O game server só lê os blocos e a cor do céu.

import (
	"encoding/json"
	"math"
	"os"
)

type Block struct {
	Position   [3]float64 `json:"position"`
	Size       [3]float64 `json:"size"`
	Color      [3]uint8   `json:"color"`
	Material   string     `json:"material"`
	Collidable *bool      `json:"collidable,omitempty"`
}

func (b Block) solid() bool {
	return b.Collidable == nil || *b.Collidable
}

type World struct {
	ID       string     `json:"id"`
	Name     string     `json:"name"`
	Author   string     `json:"author,omitempty"`
	Spawn    [3]float64 `json:"spawn"`
	SkyColor [3]uint8   `json:"sky_color"`
	Blocks   []Block    `json:"blocks"`

	// Preenchido ao carregar: só os blocos sólidos, já em min/max.
	colliders []AABB
	minY      float64
}

// AABB é a caixa de colisão de um bloco (coordenadas do mundo).
type AABB struct {
	Min [3]float64
	Max [3]float64
}

func LoadWorld(path string) (*World, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var world World
	if err := json.Unmarshal(data, &world); err != nil {
		return nil, err
	}
	world.buildColliders()
	return &world, nil
}

func (w *World) buildColliders() {
	w.colliders = w.colliders[:0]
	lowest := 0.0
	found := false
	for _, block := range w.Blocks {
		if !block.solid() {
			continue
		}
		var box AABB
		for axis := 0; axis < 3; axis++ {
			half := block.Size[axis] * 0.5
			box.Min[axis] = block.Position[axis] - half
			box.Max[axis] = block.Position[axis] + half
		}
		w.colliders = append(w.colliders, box)
		lowest = math.Min(lowest, box.Min[1])
		found = true
	}
	// Sem nenhum bloco sólido, usa o spawn como referência: qualquer queda
	// abaixo dele conta como "caiu do mundo" e o jogador volta ao início.
	if !found {
		lowest = w.Spawn[1]
	}
	// Abaixo disso o jogador "caiu do mundo" e volta ao spawn.
	w.minY = lowest - 40
}

// solidAt responde se algum bloco sólido ocupa (x, y..y+altura, z) no raio.
func (w *World) solidAt(x, y, z, radius float64) bool {
	for _, c := range w.colliders {
		if x+radius <= c.Min[0] || x-radius >= c.Max[0] {
			continue
		}
		if z+radius <= c.Min[2] || z-radius >= c.Max[2] {
			continue
		}
		if y >= c.Max[1] || y+CharacterHeight <= c.Min[1] {
			continue
		}
		return true
	}
	return false
}

// highestBelow devolve a superfície mais alta em [y-probe, y+0.02] sob os pés.
func (w *World) highestBelow(x, y, z, probe float64) (float64, bool) {
	radius := CharacterRadius - 0.01
	best := 0.0
	found := false
	for _, c := range w.colliders {
		if x+radius <= c.Min[0] || x-radius >= c.Max[0] {
			continue
		}
		if z+radius <= c.Min[2] || z-radius >= c.Max[2] {
			continue
		}
		if c.Max[1] > y+0.02 || c.Max[1] < y-probe {
			continue
		}
		if !found || c.Max[1] > best {
			best = c.Max[1]
			found = true
		}
	}
	return best, found
}

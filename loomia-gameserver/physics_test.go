package main

// Testes de paridade da física com o cliente Lua.
//
// Os cenários (mundo + inputs + posição final) ficam em `physics_golden.json`,
// gerados por este pacote e conferidos por
// `loomia-client-lovr/tests/physics_spec.lua`. Se a física mudar de um lado,
// o outro quebra no teste.

import (
	"encoding/json"
	"os"
	"testing"
)

type goldenBlock struct {
	Position   [3]float64 `json:"position"`
	Size       [3]float64 `json:"size"`
	Collidable *bool      `json:"collidable,omitempty"`
}

type goldenInput struct {
	MoveX float64 `json:"move_x"`
	MoveZ float64 `json:"move_z"`
	Jump  bool    `json:"jump"`
}

type goldenScenario struct {
	Name     string        `json:"name"`
	Blocks   []goldenBlock `json:"blocks"`
	Start    [3]float64    `json:"start"`
	OnGround bool          `json:"on_ground"`
	Inputs   []goldenInput `json:"inputs"`
	Steps    int           `json:"steps"`
	Expected struct {
		Position [3]float64 `json:"position"`
		OnGround bool       `json:"on_ground"`
	} `json:"expected"`
}

func floorBlocks() []goldenBlock {
	off := false
	return []goldenBlock{
		{Position: [3]float64{0, -0.5, 0}, Size: [3]float64{40, 1, 40}},
		{Position: [3]float64{4, 0.5, 0}, Size: [3]float64{2, 2, 6}},     // parede
		{Position: [3]float64{-4, 0.25, 0}, Size: [3]float64{2, 1.5, 6}}, // degrau baixo
		{Position: [3]float64{0, 0.25, 6}, Size: [3]float64{6, 1.5, 2}},  // caixa no caminho
		{Position: [3]float64{0, 5, 0}, Size: [3]float64{4, 0.5, 4}},     // teto
		{Position: [3]float64{9, 3, 9}, Size: [3]float64{1, 1, 1}, Collidable: &off},
	}
}

func scenarioFrom(name string, blocks []goldenBlock, start [3]float64, onGround bool, inputs []goldenInput, steps int) goldenScenario {
	s := goldenScenario{
		Name: name, Blocks: blocks, Start: start, OnGround: onGround,
		Inputs: inputs, Steps: steps,
	}
	world := &World{Spawn: start}
	for _, block := range blocks {
		world.Blocks = append(world.Blocks, Block{
			Position: block.Position, Size: block.Size, Collidable: block.Collidable,
		})
	}
	world.buildColliders()
	state := State{Pos: start, OnGround: onGround}
	for step := 0; step < steps; step++ {
		input := inputs[step%len(inputs)]
		state = world.Step(state, Input{MoveX: input.MoveX, MoveZ: input.MoveZ, Jump: input.Jump}, TickDt)
	}
	s.Expected.Position = state.Pos
	s.Expected.OnGround = state.OnGround
	return s
}

func TestWritePhysicsGolden(t *testing.T) {
	if os.Getenv("LOOMIA_WRITE_GOLDEN") != "1" {
		t.Skip("defina LOOMIA_WRITE_GOLDEN=1 para regravar physics_golden.json")
	}
	scenarios := buildScenarios()
	data, err := json.MarshalIndent(scenarios, "", "  ")
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile("physics_golden.json", append(data, '\n'), 0o644); err != nil {
		t.Fatal(err)
	}
}

func buildScenarios() []goldenScenario {
	return []goldenScenario{
		// Andar em linha reta no chão liso.
		scenarioFrom("andar em +Z", floorBlocks(), [3]float64{0, 0.2, 0}, true,
			[]goldenInput{{MoveZ: 1}}, 60),
		// Bater na parede (x = +4) e parar.
		scenarioFrom("bater na parede", floorBlocks(), [3]float64{0, 0.2, 0}, true,
			[]goldenInput{{MoveX: 1}}, 90),
		// O degrau de 1.5 é mais alto que step_height (0.6): bloqueia.
		scenarioFrom("degrau alto bloqueia", floorBlocks(), [3]float64{0, 0.2, 0}, true,
			[]goldenInput{{MoveX: -1}}, 90),
		// Pular.
		scenarioFrom("pular", floorBlocks(), [3]float64{0, 0.2, 0}, true,
			[]goldenInput{{Jump: true}, {}, {}}, 40),
		// Andar e pular ao mesmo tempo.
		scenarioFrom("andar e pular", floorBlocks(), [3]float64{0, 0.2, 0}, true,
			[]goldenInput{{MoveZ: 1}, {MoveZ: 1, Jump: true}, {MoveZ: 1}, {MoveZ: 1}}, 80),
		// Cair do spawn alto.
		scenarioFrom("cair e pousar", floorBlocks(), [3]float64{0, 8, 0}, false,
			[]goldenInput{{}}, 120),
		// Diagonal (o servidor normaliza o vetor).
		scenarioFrom("diagonal", floorBlocks(), [3]float64{0, 0.2, 0}, true,
			[]goldenInput{{MoveX: 1, MoveZ: 1}}, 60),
	}
}

func TestPhysicsGoldenMatches(t *testing.T) {
	data, err := os.ReadFile("physics_golden.json")
	if err != nil {
		t.Skip("physics_golden.json ainda não gerado")
	}
	var golden []goldenScenario
	if err := json.Unmarshal(data, &golden); err != nil {
		t.Fatal(err)
	}
	current := buildScenarios()
	if len(current) != len(golden) {
		t.Fatalf("%d cenários no golden, %d no código", len(golden), len(current))
	}
	for index, expected := range golden {
		got := current[index]
		for axis := 0; axis < 3; axis++ {
			if diff := got.Expected.Position[axis] - expected.Expected.Position[axis]; diff > 1e-9 || diff < -1e-9 {
				t.Fatalf("%s: eixo %d = %v, golden %v", expected.Name, axis, got.Expected.Position[axis], expected.Expected.Position[axis])
			}
		}
		if got.Expected.OnGround != expected.Expected.OnGround {
			t.Fatalf("%s: onGround divergente", expected.Name)
		}
	}
}

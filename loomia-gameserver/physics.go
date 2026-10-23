package main

// Física autoritativa do personagem (AABB contra os blocos do mapa).
//
// Mesmas constantes e mesma ordem de resolução que o cliente LOVR usa em
// `loomia-client-lovr/src/physics.lua`: o cliente prevê o movimento com este
// código e o servidor é quem manda na posição final.

import "math"

const (
	TickRate        = 30
	TickDt          = 1.0 / TickRate
	MoveSpeed       = 12.5
	JumpSpeed       = 40.0
	Gravity         = -95.0
	MaxFallSpeed    = -90.0
	CharacterRadius = 0.55
	CharacterHeight = 3.0
	StepHeight      = 0.6
	SnapDistance    = 0.55
)

type Input struct {
	MoveX float64 `json:"move_x"`
	MoveZ float64 `json:"move_z"`
	Jump  bool    `json:"jump"`
}

type State struct {
	Pos      [3]float64
	Vel      [3]float64
	Yaw      float64
	OnGround bool
}

func clamp(v, lo, hi float64) float64 {
	return math.Max(lo, math.Min(hi, v))
}

// Step aplica um tique de física e devolve o novo estado.
func (w *World) Step(old State, input Input, dt float64) State {
	s := old

	moveX := clamp(input.MoveX, -1, 1)
	moveZ := clamp(input.MoveZ, -1, 1)
	length := math.Hypot(moveX, moveZ)
	if length > 1 {
		moveX /= length
		moveZ /= length
	}
	s.Vel[0] = moveX * MoveSpeed
	s.Vel[2] = moveZ * MoveSpeed
	if length > 0.01 {
		s.Yaw = math.Atan2(moveX, moveZ)
	}

	wasGrounded := s.OnGround
	s.OnGround = false
	if input.Jump && wasGrounded {
		s.Vel[1] = JumpSpeed
	} else {
		s.Vel[1] += Gravity * dt
		if s.Vel[1] < MaxFallSpeed {
			s.Vel[1] = MaxFallSpeed
		}
	}

	// --- Horizontal, separando cada eixo para deslizar nas paredes. ---
	if !w.solidAt(s.Pos[0]+s.Vel[0]*dt, s.Pos[1], s.Pos[2], CharacterRadius) {
		s.Pos[0] += s.Vel[0] * dt
	} else {
		// Degrau: tenta subir até StepHeight para pegar a saliência.
		if ground, ok := w.highestBelow(
			s.Pos[0]+s.Vel[0]*dt, s.Pos[1]+StepHeight, s.Pos[2], StepHeight,
		); ok && !w.solidAt(s.Pos[0]+s.Vel[0]*dt, ground, s.Pos[2], CharacterRadius) {
			s.Pos[0] += s.Vel[0] * dt
			s.Pos[1] = ground
		} else {
			s.Vel[0] = 0
		}
	}
	if !w.solidAt(s.Pos[0], s.Pos[1], s.Pos[2]+s.Vel[2]*dt, CharacterRadius) {
		s.Pos[2] += s.Vel[2] * dt
	} else {
		if ground, ok := w.highestBelow(
			s.Pos[0], s.Pos[1]+StepHeight, s.Pos[2]+s.Vel[2]*dt, StepHeight,
		); ok && !w.solidAt(s.Pos[0], ground, s.Pos[2]+s.Vel[2]*dt, CharacterRadius) {
			s.Pos[2] += s.Vel[2] * dt
			s.Pos[1] = ground
		} else {
			s.Vel[2] = 0
		}
	}

	// --- Vertical. ---
	nextY := s.Pos[1] + s.Vel[1]*dt
	if s.Vel[1] <= 0 {
		// Caindo: procura chão entre a posição atual e a nova.
		if ground, ok := w.highestBelow(s.Pos[0], s.Pos[1], s.Pos[2], s.Pos[1]-nextY+0.02); ok {
			nextY = ground
			s.Vel[1] = 0
			s.OnGround = true
		}
	} else if w.solidAt(s.Pos[0], nextY, s.Pos[2], CharacterRadius) {
		// Subindo: bateu na parte de baixo de um bloco.
		s.Vel[1] = 0
		nextY = s.Pos[1]
	}
	s.Pos[1] = nextY

	// Varredura de chão para continuar colado ao piso ao andar em rampas.
	if !s.OnGround && math.Abs(s.Vel[1]) < 1.0 {
		if ground, ok := w.highestBelow(s.Pos[0], s.Pos[1], s.Pos[2], SnapDistance); ok {
			s.Pos[1] = ground
			s.Vel[1] = 0
			s.OnGround = true
		}
	}

	return s
}

// FellOut responde se o jogador caiu abaixo do mapa.
func (w *World) FellOut(s State) bool {
	return s.Pos[1] < w.minY
}

func (w *World) SpawnState() State {
	return State{Pos: w.Spawn}
}

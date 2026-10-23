package main

// Testes do protocolo: o `protocol_golden.json` é a fonte única compartilhada
// com o cliente Lua (o `tests/protocol_spec.lua` lê o mesmo arquivo).

import (
	"encoding/json"
	"os"
	"strings"
	"testing"
)

type goldenFile struct {
	Constants      map[string]float64 `json:"constants"`
	ClientToServer []json.RawMessage  `json:"client_to_server"`
	ServerToClient []json.RawMessage  `json:"server_to_client"`
}

func loadGolden(t *testing.T) goldenFile {
	t.Helper()
	data, err := os.ReadFile("protocol_golden.json")
	if err != nil {
		t.Fatalf("protocol_golden.json: %v", err)
	}
	var goldenFile
	if err := json.Unmarshal(data, &golden); err != nil {
		t.Fatalf("protocol_golden.json inválido: %v", err)
	}
	return golden
}

// O servidor precisa decodificar TODAS as mensagens do cliente.
func TestDecodeClientMessages(t *testing.T) {
	golden := loadGolden(t)
	if len(golden.ClientToServer) == 0 {
		t.Fatal("nenhuma mensagem client_to_server no golden")
	}
	for _, raw := range golden.ClientToServer {
		var msg Msg
		if err := json.Unmarshal(raw, &msg); err != nil {
			t.Fatalf("%s: %v", raw, err)
	}
		if strings.TrimSpace(msg.T) == "" {
			t.Fatalf("%s: campo \"t\" vazio", raw)
	}
		switch msg.T {
		case "hello":
			if msg.Token != "abc" || msg.Name != "Ana" {
				t.Fatalf("hello mal decodificado: %+v", msg)
			}
			if len(msg.Appearance) == 0 || msg.Appearance[0] != '{' {
				t.Fatalf("appearance deveria virar RawMessage de objeto: %q", msg.Appearance)
			}
		case "input":
			if msg.Seq != 12 || msg.MoveX != 1 || msg.Yaw != 0.4 {
				t.Fatalf("input mal decodificado: %+v", msg)
			}
		case "ping":
			if msg.ID != 7 || msg.Time != 123.45 {
				t.Fatalf("ping mal decodificado: %+v", msg)
			}
	}
	}
}

// As mensagens serializadas pelo servidor têm que preservar os campos do golden.
func TestServerMessagesRoundTrip(t *testing.T) {
	golden := loadGolden(t)
	for _, raw := range golden.ServerToClient {
		var original map[string]any
		if err := json.Unmarshal(raw, &original); err != nil {
			t.Fatalf("%s: %v", raw, err)
	}
	payload, err := encodeJSON(original)
		if err != nil {
			t.Fatalf("encodeJSON: %v", err)
	}
	// `encodeJSON` termina com newline (modo TCP).
	payload = payload[:len(payload)-1]
		var back map[string]any
		if err := json.Unmarshal(payload, &back); err != nil {
			t.Fatalf("round-trip: %v", err)
	}
		if back["t"] != original["t"] {
			t.Fatalf("tipo perdido: %v != %v", back["t"], original["t"])
	}
	}
}

// As constantes de física do Go batem com o golden (e com o cliente Lua).
func TestPhysicsConstantsMatchGolden(t *testing.T) {
	golden := loadGolden(t)
	expected := map[string]float64{
	"tick_rate":        float64(TickRate),
	"move_speed":       MoveSpeed,
	"jump_speed":       JumpSpeed,
	"gravity":          Gravity,
	"max_fall_speed":   MaxFallSpeed,
	"character_radius": CharacterRadius,
	"character_height": CharacterHeight,
	"step_height":      StepHeight,
	"snap_distance":    SnapDistance,
	"eye_height":       1.55,
	}
	for name, value := range expected {
	got, ok := golden.Constants[name]
		if !ok {
			t.Fatalf("constante %q ausente no golden", name)
	}
		if got != value {
			t.Fatalf("constante %q: golden %v, Go %v", name, got, value)
	}
	}
}

// Regra do protocolo: o snapshot NÃO traz ack de topo nem a própria posição.
func TestSnapshotHasOnlyOthers(t *testing.T) {
	players := []SnapshotEntry{{ID: 7, Ack: 3, P: [3]float64{1, 2, 3}}}
	payload := map[string]any{"t": "snapshot", "tick": 9, "players": players}
	encoded, err := json.Marshal(payload)
	if err != nil {
		t.Fatal(err)
	}
	var decoded map[string]any
	if err := json.Unmarshal(encoded, &decoded); err != nil {
		t.Fatal(err)
	}
	if _, ok := decoded["ack"]; ok {
		t.Fatal("snapshot não pode ter ack de topo (o ack vem em players[].ack)")
	}
	list, ok := decoded["players"].([]any)
	if !ok || len(list) != 1 {
		t.Fatalf("players inválido: %v", decoded["players"])
	}
	if got := list[0].(map[string]any)["ack"]; got != float64(3) {
		t.Fatalf("players[0].ack = %v", got)
	}

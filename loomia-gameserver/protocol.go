package main

// Protocolo do game server (UDP, datagramas JSON).
//
// O UDP só transporta pacotes pequenos e frequentes; mensagens grandes (mapa,
// histórico de chat) vão pelo TCP do master via `GET /api/maps/{id}`.
//
// Direção cliente → servidor (um datagrama ou uma linha TCP por mensagem):
//
//	{"t":"hello","token":"...","name":"Ana","appearance":{...}}
//	{"t":"input","seq":12,"move_x":1,"move_z":0,"jump":false,"yaw":0.4}
//	{"t":"chat","text":"oi"}
//	{"t":"respawn"}
//	{"t":"ping","id":7,"time":1234.5}
//
// Direção servidor → cliente:
//
//	{"t":"welcome","id":3,"tick":120,"spawn":[...],"map_name":"...","you":{...},"world_version":"..."}
//	{"t":"snapshot","tick":121,"players":[{"id":3,"ack":12,"p":[...],"v":[...],"yaw":0,"ground":true,"appearance":{...}}]}
//	{"t":"players","players":[...]}          (quando alguém entra/sai)
//	{"t":"chat","from":"Ana","text":"oi"}
//	{"t":"pong","id":7,"time":1234.5,"server_time":...}
//	{"t":"respawn","p":[...]}
//	{"t":"error","message":"..."}

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"strings"
	"time"
)

type Msg struct {
	T          string          `json:"t"`
	Token      string          `json:"token,omitempty"`
	Name       string          `json:"name,omitempty"`
	Appearance json.RawMessage `json:"appearance,omitempty"`
	Seq        uint32          `json:"seq,omitempty"`
	MoveX      float64         `json:"move_x,omitempty"`
	MoveZ      float64         `json:"move_z,omitempty"`
	Jump       bool            `json:"jump,omitempty"`
	Yaw        float64         `json:"yaw,omitempty"`
	Text       string          `json:"text,omitempty"`
	ID         uint32          `json:"id,omitempty"`
	Time       float64         `json:"time,omitempty"`
}

// Msg recebida do cliente.
//
// O `appearance` é VALIDADO à mão (tamanho e tipo) em vez de usar
// `json.RawMessage` com tag: o encoding/json procura a tag no ponteiro do tipo
// antes de chamar o UnmarshalJSON deste método ("json: cannot unmarshal ..."),
// o que quebrava o `hello` inteiro e a sala não respondia a ninguém.
func (m *Msg) UnmarshalJSON(data []byte) error {
	var wire struct {
		T     string `json:"t"`
		Token string `json:"token,omitempty"`
		Name  string `json:"name,omitempty"`
		// Decodificado no mapa cru para preservar números/strings exatamente.
		Appearance map[string]json.RawMessage `json:"appearance,omitempty"`
		Seq        uint32                     `json:"seq,omitempty"`
		MoveX      float64                    `json:"move_x,omitempty"`
		MoveZ      float64                    `json:"move_z,omitempty"`
		Jump       bool                       `json:"jump,omitempty"`
		Yaw        float64                    `json:"yaw,omitempty"`
		Text       string                     `json:"text,omitempty"`
		ID         uint32                     `json:"id,omitempty"`
		Time       float64                    `json:"time,omitempty"`
	}
	if err := json.Unmarshal(data, &wire); err != nil {
		return err
	}
	*m = Msg{
		T: wire.T, Token: wire.Token, Name: wire.Name, Seq: wire.Seq,
		MoveX: wire.MoveX, MoveZ: wire.MoveZ, Jump: wire.Jump, Yaw: wire.Yaw,
		Text: wire.Text, ID: wire.ID, Time: wire.Time,
	}
	if len(wire.Appearance) > 0 {
		if encoded, err := json.Marshal(wire.Appearance); err == nil && len(encoded) <= 4096 {
			m.Appearance = encoded
		}
	}
	return nil
}

// SnapshotEntry é o estado de um jogador dentro de um snapshot.
type SnapshotEntry struct {
	ID         uint32          `json:"id"`
	Ack        uint32          `json:"ack"`
	P          [3]float64      `json:"p"`
	V          [3]float64      `json:"v"`
	Yaw        float64         `json:"yaw"`
	Ground     bool            `json:"ground"`
	Appearance json.RawMessage `json:"appearance,omitempty"`
}

type PlayerInfo struct {
	ID         uint32          `json:"id"`
	Name       string          `json:"name"`
	Appearance json.RawMessage `json:"appearance,omitempty"`
}

// worldVersion identifica o mapa (o cliente compara antes de renderizar), no
// mesmo formato `id-sha256[:12]` usado pelo master.
func worldVersion(w *World) string {
	digest := sha256.Sum256([]byte(w.ID + "|" + fmt.Sprint(len(w.Blocks))))
	return w.ID + "-" + hex.EncodeToString(digest[:])[:12]
}

// postJSON envia um POST JSON best-effort (relatórios para o master).
func postJSON(url string, body any) {
	payload, err := json.Marshal(body)
	if err != nil {
		return
	}
	client := &http.Client{Timeout: 3 * time.Second}
	req, err := http.NewRequest(http.MethodPost, url, strings.NewReader(string(payload)))
	if err != nil {
		return
	}
	req.Header.Set("Content-Type", "application/json")
	resp, err := client.Do(req)
	if err != nil {
		return
	}
	_ = resp.Body.Close()
}

// encodeJSON serializa com newline (o modo TCP usa um JSON por linha).
func encodeJSON(value any) ([]byte, error) {
	payload, err := json.Marshal(value)
	if err != nil {
		return nil, err
	}
	return append(payload, '\n'), nil
}

// writeJSON serializa e envia um datagrama (sem erro fatal: UDP é best-effort).
func writeJSON(conn *net.UDPConn, addr *net.UDPAddr, value any) {
	payload, err := json.Marshal(value)
	if err != nil {
		log.Printf("json: %v", err)
		return
	}
	if len(payload) > 65000 {
		log.Printf("datagrama grande demais (%d bytes): descartado", len(payload))
		return
	}
	if _, err := conn.WriteToUDP(payload, addr); err != nil {
		log.Printf("udp write: %v", err)
	}
}

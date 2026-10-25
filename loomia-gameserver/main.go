package main

// Sala autoritativa de um mapa. Uma instância deste processo = uma sala; o
// master server aloca uma porta por mapa/sala e passa as credenciais na
// linha de comando.
//
//	go run . -map ../data/worlds/baseplate.loom -port 7801 -token abc -secret xyz
//
// O `-token` é o token de entrada da sala (o site o entrega ao cliente no link
// `loomia://launch?...`). O `-secret` autentica o game server no master para
// reportar presença e chat. O transporte do jogo é UDP; `-tcp` liga um modo de
// diagnóstico com as mesmas mensagens JSON.

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
)

const (
	maxPlayers       = 32
	helloTimeout     = 10 * time.Second
	playerTimeout    = 15 * time.Second
	maxChatLen       = 200
	chatHistoryLimit = 40
	maxNameLen       = 20
)

type client struct {
	key      string
	addr     *net.UDPAddr
	peer     *tcpPeer
	id       uint32
	name     string
	look     json.RawMessage
	session  string
	inGame   bool
	state    State
	lastSeq  uint32
	lastSeen time.Time
}

// tcpPeer é uma conexão TCP no modo de diagnóstico (`-tcp`).
type tcpPeer struct {
	conn net.Conn
	mu   sync.Mutex
}

func (p *tcpPeer) write(payload []byte) {
	p.mu.Lock()
	defer p.mu.Unlock()
	_, _ = p.conn.Write(payload)
}

type GameServer struct {
	world            *World
	token            string
	secret           string
	master           string
	session          string
	port             int
	bind             string
	conn             *net.UDPConn
	tcp              bool
	peers            map[string]*tcpPeer
	peersMu          sync.Mutex
	mu               sync.Mutex
	clients          map[string]*client
	nextID           uint32
	chat             []chatLine
	stop             chan struct{}
	snapTick         int64
	lastReportedChat chatLine
}

type chatLine struct {
	From string `json:"from"`
	Text string `json:"text"`
}

func main() {
	var (
		mapPath = flag.String("map", "../data/worlds/baseplate.loom", "caminho do mapa .loom")
		port    = flag.Int("port", 7801, "porta UDP da sala")
		bind    = flag.String("bind", "0.0.0.0", "endereço de escuta")
		token   = flag.String("token", "", "token de entrada da sala")
		secret  = flag.String("secret", "", "segredo compartilhado com o master")
		master  = flag.String("master", "http://127.0.0.1:8080", "URL do master server")
		session = flag.String("session", "", "id da sessão no master")
		tcpMode = flag.Bool("tcp", false, "modo TCP de diagnóstico (o jogo usa UDP)")
	)
	flag.Parse()

	world, err := LoadWorld(*mapPath)
	if err != nil {
		log.Fatalf("mapa: %v", err)
	}

	addr := &net.UDPAddr{IP: net.ParseIP(*bind), Port: *port}
	var conn *net.UDPConn
	if !*tcpMode {
		// No Linux, `ListenUDP` usa SO_REUSEADDR e DUAS salas podem escutar a
		// mesma porta: a segunda rouba os pacotes e o cliente nunca recebe o
		// `welcome`. Aqui o reuso é desligado para o bind falhar de imediato.
		listenConfig := net.ListenConfig{
			Control: func(network, address string, raw syscall.RawConn) error {
				return raw.Control(func(fd uintptr) {
					_ = syscall.SetsockoptInt(int(fd), syscall.SOL_SOCKET, syscall.SO_REUSEADDR, 0)
				})
			},
		}
		packetConn, err := listenConfig.ListenPacket(context.Background(), "udp", addr.String())
		if err != nil {
			log.Fatalf("udp: %v", err)
		}
		conn = packetConn.(*net.UDPConn)
		defer conn.Close()
		_ = conn.SetReadBuffer(1 << 20)
	}

	server := &GameServer{
		world:   world,
		token:   *token,
		secret:  *secret,
		master:  strings.TrimRight(*master, "/"),
		session: *session,
		port:    *port,
		bind:    *bind,
		conn:    conn,
		tcp:     *tcpMode,
		clients: map[string]*client{},
		stop:    make(chan struct{}),
	}

	log.Printf("sala %q no ar em %s:%d (%d blocos)", world.Name, *bind, *port, len(world.Blocks))

	if server.tcp {
		go server.tcpIO()
	} else {
		go server.readLoop()
	}
	go server.tickLoop()
	go server.reportLoop()

	signals := make(chan os.Signal, 1)
	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM)
	<-signals
	close(server.stop)
	server.shutdown()
	log.Printf("sala encerrada")
}

func (gs *GameServer) readLoop() {
	buffer := make([]byte, 4096)
	for {
		n, addr, err := gs.conn.ReadFromUDP(buffer)
		if err != nil {
			select {
			case <-gs.stop:
				return
			default:
				continue
			}
		}
		var msg Msg
		// O datagrama pode ser sobrescrito logo após o retorno; copia o trecho
		// antes de decodificar qualquer cosa que guarde referência.
		payload := append([]byte(nil), buffer[:n]...)
		if err := json.Unmarshal(payload, &msg); err != nil {
			log.Printf("datagrama inválido de %s: %v (%q)", addr, err, string(payload))
			continue
		}
		gs.handle(addr, nil, &msg)
	}
}

func (gs *GameServer) key(addr *net.UDPAddr) string {
	if addr != nil {
		return addr.String()
	}
	return "tcp"
}

// tcpIO atende o modo de diagnóstico `-tcp`: cada conexão manda mensagens JSON
// concatenadas e recebe os snapshots de volta.
func (gs *GameServer) tcpIO() {
	listener, err := net.Listen("tcp", fmt.Sprintf("%s:%d", gs.bind, gs.port))
	if err != nil {
		// Erro explícito no log: o master detecta e não espera 30 s.
		log.Fatalf("tcp: %v", err)
	}
	log.Printf("tcp de diagnóstico escutando em %s:%d", gs.bind, gs.port)
	go func() {
		<-gs.stop
		_ = listener.Close()
	}()
	for {
		conn, err := listener.Accept()
		if err != nil {
			return
		}
		peer := &tcpPeer{conn: conn}
		gs.peersMu.Lock()
		if gs.peers == nil {
			gs.peers = map[string]*tcpPeer{}
		}
		gs.peers[conn.RemoteAddr().String()] = peer
		gs.peersMu.Unlock()
		go func(c net.Conn, p *tcpPeer) {
			defer c.Close()
			// Uma mensagem JSON por linha.
			scanner := bufio.NewScanner(c)
			scanner.Buffer(make([]byte, 0, 64*1024), 1<<20)
			for scanner.Scan() {
				line := bytes.TrimSpace(scanner.Bytes())
				if len(line) == 0 {
					continue
				}
				var msg Msg
				if err := json.Unmarshal(line, &msg); err != nil {
					log.Printf("tcp: mensagem inválida de %s: %v", c.RemoteAddr(), err)
					continue
				}
				gs.handle(nil, p, &msg)
			}
			gs.dropPeer(p)
		}(conn, peer)
	}
}

func (gs *GameServer) dropPeer(peer *tcpPeer) {
	addr := peer.conn.RemoteAddr().String()
	gs.peersMu.Lock()
	delete(gs.peers, addr)
	gs.peersMu.Unlock()
	gs.mu.Lock()
	for key, c := range gs.clients {
		if c.peer == peer {
			delete(gs.clients, key)
		}
	}
	gs.mu.Unlock()
	_ = peer.conn.Close()
}

func (gs *GameServer) send(addr *net.UDPAddr, peer *tcpPeer, value any) {
	if peer != nil {
		if payload, err := encodeJSON(value); err == nil {
			peer.write(payload)
		}
		return
	}
	writeJSON(gs.conn, addr, value)
}

func (gs *GameServer) handle(addr *net.UDPAddr, peer *tcpPeer, msg *Msg) {
	key := gs.key(addr)
	if peer != nil {
		key = "tcp:" + peer.conn.RemoteAddr().String()
	}

	gs.mu.Lock()
	defer gs.mu.Unlock()

	c := gs.clients[key]
	if msg.T != "hello" {
		if c == nil || !c.inGame {
			gs.send(addr, peer, map[string]string{"t": "error", "message": "faça hello primeiro"})
			return
		}
		c.lastSeen = time.Now()
	}

	switch msg.T {
	case "hello":
		gs.handleHello(addr, peer, key, msg)
		return
	case "input":
		if msg.Seq <= c.lastSeq {
			return // já processado (UDP duplicado/reordenado)
		}
		c.lastSeq = msg.Seq
		c.state = gs.world.Step(c.state, Input{MoveX: msg.MoveX, MoveZ: msg.MoveZ, Jump: msg.Jump}, TickDt)
		if msg.Yaw != 0 {
			c.state.Yaw = msg.Yaw
		}
		if gs.world.FellOut(c.state) {
			gs.respawn(c)
		}
	case "ping":
		gs.send(addr, peer, map[string]any{
			"t": "pong", "id": msg.ID, "time": msg.Time,
			"server_time": float64(time.Now().UnixMilli()) / 1000,
		})
	case "chat":
		text := sanitizeText(msg.Text)
		if text != "" {
			from := c.name
			// O mutex já está preso: `pushChat`/`broadcast` também o usam.
			gs.chat = append(gs.chat, chatLine{From: from, Text: text})
			if len(gs.chat) > chatHistoryLimit {
				gs.chat = gs.chat[len(gs.chat)-chatHistoryLimit:]
			}
			go gs.broadcast(map[string]any{"t": "chat", "from": from, "text": text})
		}
	case "respawn":
		gs.respawn(c)
		gs.send(addr, peer, map[string]any{"t": "respawn", "p": c.state.Pos})
	case "bye":
		delete(gs.clients, key)
		// `broadcastPlayers` pega o mutex: chamar em goroutine evita o
		// deadlock (o `handle` ainda o segura neste ponto).
		go gs.broadcastPlayers()
	}
}

func (gs *GameServer) handleHello(addr *net.UDPAddr, peer *tcpPeer, key string, msg *Msg) {
	if peer != nil {
		// No TCP cada conexão é um cliente (o endereço remoto pode se repetir
		// em proxies/NAT); o prefixo evita colisão com o formato UDP.
		key = "tcp:" + peer.conn.RemoteAddr().String()
	}
	if gs.token != "" && msg.Token != gs.token {
		gs.send(addr, peer, map[string]string{"t": "error", "message": "token inválido"})
		return
	}
	if c := gs.clients[key]; c != nil {
		// Repete o estado: o datagrama de hello pode ter se perdido.
		gs.sendWelcome(c)
		return
	}

	// Sessão única: o mesmo jogador (token de conta) não entra duas vezes.
	// Cada sala tem um token próprio, mas o hello carrega também o token de
	// sessão da conta (campo `session`); se já há um cliente com ele, é clone.
	if msg.Session != "" {
		for _, other := range gs.clients {
			if other.inGame && other.session == msg.Session {
				gs.send(addr, peer, map[string]string{
					"t":     "error",
					"message": "sessão já ativa nesta sala",
				})
				return
			}
		}
	}

	inRoom := 0
	for _, other := range gs.clients {
		if other.inGame {
			inRoom++
		}
	}
	if inRoom >= maxPlayers {
		gs.send(addr, peer, map[string]string{"t": "error", "message": "sala cheia"})
		return
	}

	gs.nextID++
	name := sanitizeName(msg.Name)
	if name == "" {
		name = "Jogador"
	}

	c := &client{
		key:      key,
		addr:     addr,
		peer:     peer,
		id:       gs.nextID,
		name:     name,
		look:     msg.Appearance,
		session:  msg.Session,
		inGame:   true,
		state:    gs.world.SpawnState(),
		lastSeen: time.Now(),
	}
	gs.clients[key] = c
	log.Printf("entrou %q (id %d) de %s", name, c.id, gs.key(addr))
	gs.sendWelcome(c)
	// O anúncio de presença é enviado DEPOIS de liberar o mutex (este
	// handleHello roda com ele preso). Em goroutine, para o welcome sair
	// primeiro e o anúncio não depender do lock.
	go gs.broadcastPlayers()
	go gs.broadcast(map[string]any{"t": "chat", "from": "Loomia", "text": name + " entrou na experiência."})
}

func (gs *GameServer) sendWelcome(c *client) {
	players := make([]map[string]any, 0, len(gs.clients))
	for _, other := range gs.clients {
		if other.id == c.id || !other.inGame {
			continue
		}
		players = append(players, map[string]any{
			"id": other.id, "name": other.name, "appearance": other.look,
			"p": other.state.Pos, "yaw": other.state.Yaw,
		})
	}
	gs.send(c.addr, c.peer, map[string]any{
		"t":             "welcome",
		"id":            c.id,
		"tick":          atomic.LoadInt64(&gs.snapTick),
		"tick_rate":     TickRate,
		"map_id":        gs.world.ID,
		"map_name":      gs.world.Name,
		"world_version": worldVersion(gs.world),
		"spawn":         gs.world.Spawn,
		"sky_color":     gs.world.SkyColor,
		"you": map[string]any{
			"p": c.state.Pos, "v": c.state.Vel, "yaw": c.state.Yaw, "ground": c.state.OnGround,
		},
		"players": players,
		"chat":    gs.chat,
	})
}

func (gs *GameServer) respawn(c *client) {
	c.state = gs.world.SpawnState()
}

func (gs *GameServer) tickLoop() {
	ticker := time.NewTicker(time.Second / TickRate)
	defer ticker.Stop()
	for {
		select {
		case <-gs.stop:
			return
		case <-ticker.C:
			gs.tick()
		}
	}
}

func (gs *GameServer) tick() {
	tick := atomic.AddInt64(&gs.snapTick, 1)

	gs.mu.Lock()
	now := time.Now()
	if len(gs.clients) == 0 {
		gs.mu.Unlock()
		return
	}

	entries := make([]SnapshotEntry, 0, len(gs.clients))
	dropped := make([]*client, 0)
	// Envios montados sob lock e despachados depois dele (ver abaixo).
	type delivery struct {
		addr   *net.UDPAddr
		peer   *tcpPeer
		ack    uint32
		others []SnapshotEntry
	}
	deliveries := make([]delivery, 0, len(gs.clients))
	for key, c := range gs.clients {
		if !c.inGame {
			if now.Sub(c.lastSeen) > helloTimeout {
				delete(gs.clients, key)
			}
			continue
		}
		if now.Sub(c.lastSeen) > playerTimeout {
			log.Printf("timeout %q (id %d)", c.name, c.id)
			delete(gs.clients, key)
			dropped = append(dropped, c)
			continue
		}
		entries = append(entries, SnapshotEntry{
			ID: c.id, Ack: c.lastSeq, P: c.state.Pos, V: c.state.Vel,
			Yaw: c.state.Yaw, Ground: c.state.OnGround, Appearance: c.look,
		})
	}

	for _, c := range gs.clients {
		if !c.inGame {
			continue
		}
		// Cada cliente recebe todos EXCETO ele mesmo (a posição dele vive na
		// sua previsão local; só o `ack` importa para a reconciliação).
		others := make([]SnapshotEntry, 0, len(entries))
		for _, entry := range entries {
			if entry.ID != c.id {
				others = append(others, entry)
			}
		}
		deliveries = append(deliveries, delivery{addr: c.addr, peer: c.peer, ack: c.lastSeq, others: others})
	}
	gs.mu.Unlock()

	// O I/O de UDP acontece FORA do mutex: com ele preso, um socket cheio
	// travava a sala inteira (o `readLoop` nunca mais pegava o lock).
	for _, entry := range deliveries {
		gs.send(entry.addr, entry.peer, map[string]any{
			"t":       "snapshot",
			"tick":    tick,
			"ack":     entry.ack,
			"players": entry.others,
		})
	}

	// O `players` anuncia quem entrou/saiu. Fora do mutex: broadcastPlayers
	// também lê a lista de clientes.
	if len(dropped) > 0 {
		gs.broadcastPlayers()
	}
}

func (gs *GameServer) broadcastPlayers() {
	gs.mu.Lock()
	players := make([]PlayerInfo, 0, len(gs.clients))
	targets := make([]*client, 0, len(gs.clients))
	for _, c := range gs.clients {
		if c.inGame {
			players = append(players, PlayerInfo{ID: c.id, Name: c.name, Appearance: c.look})
			targets = append(targets, c)
		}
	}
	gs.mu.Unlock()
	payload := map[string]any{"t": "players", "players": players}
	for _, c := range targets {
		gs.send(c.addr, c.peer, payload)
	}
}

// broadcast envia para todos os jogadores ativos (o mutex deve estar LIVRE).
func (gs *GameServer) broadcast(payload map[string]any) {
	for _, c := range gs.activeClients() {
		gs.send(c.addr, c.peer, payload)
	}
}

// activeClients cópia a lista sem segurar o mutex durante o I/O.
func (gs *GameServer) activeClients() []*client {
	gs.mu.Lock()
	defer gs.mu.Unlock()
	targets := make([]*client, 0, len(gs.clients))
	for _, c := range gs.clients {
		if c.inGame {
			targets = append(targets, c)
		}
	}
	return targets
}

func (gs *GameServer) pushChat(from, text string) {
	gs.mu.Lock()
	gs.chat = append(gs.chat, chatLine{From: from, Text: text})
	if len(gs.chat) > chatHistoryLimit {
		gs.chat = gs.chat[len(gs.chat)-chatHistoryLimit:]
	}
	gs.mu.Unlock()
}

// reportLoop avisa o master de tempos em tempos (presença/chat para o site).
func (gs *GameServer) reportLoop() {
	if gs.master == "" || gs.session == "" {
		return
	}
	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-gs.stop:
			return
		case <-ticker.C:
			gs.report()
		}
	}
}

func (gs *GameServer) report() {
	gs.mu.Lock()
	names := make([]string, 0, len(gs.clients))
	for _, c := range gs.clients {
		if c.inGame {
			names = append(names, c.name)
		}
	}
	lastChat := chatLine{}
	if len(gs.chat) > 0 {
		lastChat = gs.chat[len(gs.chat)-1]
	}
	gs.mu.Unlock()

	body := map[string]any{
		"session": gs.session,
		"secret":  gs.secret,
		"port":    gs.port,
		"players": names,
	}
	// O chat só vai uma vez (o master acumula o histórico).
	if lastChat.Text != "" && lastChat != gs.lastReportedChat {
		gs.lastReportedChat = lastChat
		body["chat"] = lastChat
	}
	postJSON(gs.master+"/api/internal/rooms/report", body)
}

func (gs *GameServer) shutdown() {
	gs.broadcast(map[string]any{"t": "error", "message": "sala encerrada"})
	if gs.master != "" && gs.session != "" {
		postJSON(gs.master+"/api/internal/rooms/close", map[string]any{
			"session": gs.session, "secret": gs.secret, "port": gs.port,
		})
	}
}

func sanitizeName(name string) string {
	name = strings.TrimSpace(name)
	name = strings.Map(func(r rune) rune {
		if r < 32 {
			return -1
		}
		return r
	}, name)
	if len([]rune(name)) > maxNameLen {
		name = string([]rune(name)[:maxNameLen])
	}
	return name
}

func sanitizeText(text string) string {
	text = strings.TrimSpace(text)
	text = strings.ReplaceAll(text, "\n", " ")
	text = strings.Map(func(r rune) rune {
		if r < 32 {
			return -1
		}
		return r
	}, text)
	if len([]rune(text)) > maxChatLen {
		text = string([]rune(text)[:maxChatLen])
	}
	return text
}

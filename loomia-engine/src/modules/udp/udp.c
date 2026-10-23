/**
 * Módulo `udp` do Loomia Engine — implementação (POSIX + Winsock).
 *
 * Um único socket por processo (a sessão do jogador). Tudo é não-bloqueante:
 * `receive` devolve `nil, "timeout"` quando não há datagrama, o que permite
 * chamá-lo uma vez por frame sem travar o jogo.
 */

#include "udp.h"

#include <string.h>
#include <stdio.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET Socket;
  #define INVALID_SOCKET_VALUE INVALID_SOCKET
  #define close_socket closesocket
  #define last_error() WSAGetLastError()
  #define WOULD_BLOCK(err) ((err) == WSAEWOULDBLOCK)
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <errno.h>
  #include <fcntl.h>
  typedef int Socket;
  #define INVALID_SOCKET_VALUE (-1)
  #define close_socket close
  #define last_error() errno
  #define WOULD_BLOCK(err) ((err) == EAGAIN || (err) == EWOULDBLOCK)
#endif

#include "lua.h"
#include "lauxlib.h"
#include "util.h"

#define UDP_MAX_DATAGRAM 65535
#define UDP_TIMEOUT_ERROR "timeout"

static Socket udpSocket = INVALID_SOCKET_VALUE;
static struct sockaddr_in udpPeer;
static int udpConnected = 0;

/* ------------------------------------------------------------------------- */
/* Sistema                                                                   */
/* ------------------------------------------------------------------------- */

static int udpSystemInit(void) {
#ifdef _WIN32
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
  return 1;
#endif
}

static void udpSystemShutdown(void) {
#ifdef _WIN32
  WSACleanup();
#endif
}

static void udpCloseSocket(void) {
  if (udpSocket != INVALID_SOCKET_VALUE) {
    close_socket(udpSocket);
    udpSocket = INVALID_SOCKET_VALUE;
  }
  udpConnected = 0;
}

static int udpSetNonBlocking(Socket socket) {
#ifdef _WIN32
  u_long mode = 1;
  return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
  int flags = fcntl(socket, F_GETFL, 0);
  return flags != -1 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

/* ------------------------------------------------------------------------- */
/* Lua                                                                       */
/* ------------------------------------------------------------------------- */

static int l_udpConnect(lua_State* L) {
  const char* host = luaL_checkstring(L, 1);
  int port = (int) luaL_checkinteger(L, 2);

  if (port <= 0 || port > 65535) {
    lua_pushnil(L);
    lua_pushstring(L, "porta inválida");
    return 2;
  }

  /* Resolve o host (aceita IP e nome DNS). */
  char portString[8];
  snprintf(portString, sizeof(portString), "%d", port);
  struct addrinfo hints;
  struct addrinfo* result = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(host, portString, &hints, &result) != 0 || !result) {
    lua_pushnil(L);
    lua_pushfstring(L, "não consegui resolver %s", host);
    return 2;
  }
  memcpy(&udpPeer, result->ai_addr, sizeof(udpPeer));
  freeaddrinfo(result);

  udpCloseSocket();

  Socket handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (handle == INVALID_SOCKET_VALUE) {
    lua_pushnil(L);
    lua_pushstring(L, "não consegui criar o socket UDP");
    return 2;
  }

  if (!udpSetNonBlocking(handle)) {
    close_socket(handle);
    lua_pushnil(L);
    lua_pushstring(L, "não consegui configurar o socket como não bloqueante");
    return 2;
  }

  /* Buffer grande: snapshots com 32 jogadores passam de 1 KB. */
  int buffer = 1 << 20;
  setsockopt(handle, SOL_SOCKET, SO_RCVBUF, (const char*) &buffer, sizeof(buffer));
  setsockopt(handle, SOL_SOCKET, SO_SNDBUF, (const char*) &buffer, sizeof(buffer));

  udpSocket = handle;
  udpConnected = 1;

  lua_pushboolean(L, 1);
  return 1;
}

static int l_udpSend(lua_State* L) {
  size_t length = 0;
  const char* data = luaL_checklstring(L, 1, &length);

  if (!udpConnected || udpSocket == INVALID_SOCKET_VALUE) {
    lua_pushnil(L);
    lua_pushstring(L, "socket fechado (chame udp.connect primeiro)");
    return 2;
  }

  if (length == 0 || length > UDP_MAX_DATAGRAM) {
    lua_pushnil(L);
    lua_pushfstring(L, "datagrama com %d bytes (máximo %d)", (int) length, UDP_MAX_DATAGRAM);
    return 2;
  }

  /* Permite mandar para outro endereço sem reconectar (opcional). */
  struct sockaddr_in target = udpPeer;
  if (!lua_isnoneornil(L, 2)) {
    const char* host = luaL_checkstring(L, 2);
    int port = (int) luaL_checkinteger(L, 3);
    target.sin_family = AF_INET;
    target.sin_port = htons((unsigned short) port);
    if (inet_pton(AF_INET, host, &target.sin_addr) != 1) {
      lua_pushnil(L);
      lua_pushfstring(L, "endereço IPv4 inválido: %s", host);
      return 2;
    }
  }

  long sent = (long) sendto(udpSocket, data, (int) length, 0,
                            (const struct sockaddr*) &target, sizeof(target));
  if (sent < 0) {
    int error = last_error();
    if (WOULD_BLOCK(error)) {
      lua_pushnil(L);
      lua_pushstring(L, "socket ocupado (tente novamente)");
    } else {
      lua_pushnil(L);
      lua_pushfstring(L, "sendto falhou (%d)", error);
    }
    return 2;
  }

  lua_pushinteger(L, sent);
  return 1;
}

static int l_udpReceive(lua_State* L) {
  if (!udpConnected || udpSocket == INVALID_SOCKET_VALUE) {
    lua_pushnil(L);
    lua_pushstring(L, "socket fechado (chame udp.connect primeiro)");
    return 2;
  }

  static char buffer[UDP_MAX_DATAGRAM];
  struct sockaddr_in from;
#ifdef _WIN32
  int fromLength = sizeof(from);
#else
  socklen_t fromLength = sizeof(from);
#endif

  long received = (long) recvfrom(udpSocket, buffer, sizeof(buffer), 0,
                                  (struct sockaddr*) &from, &fromLength);
  if (received < 0) {
    int error = last_error();
    lua_pushnil(L);
    lua_pushstring(L, WOULD_BLOCK(error) ? UDP_TIMEOUT_ERROR : "recvfrom falhou");
    return 2;
  }

  lua_pushlstring(L, buffer, (size_t) received);

  char address[32];
  char ip[INET_ADDRSTRLEN] = "0.0.0.0";
  inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
  snprintf(address, sizeof(address), "%s:%d", ip, (int) ntohs(from.sin_port));
  lua_pushstring(L, address);
  return 2;
}

static int l_udpPort(lua_State* L) {
  if (!udpConnected || udpSocket == INVALID_SOCKET_VALUE) {
    lua_pushnil(L);
    return 1;
  }
  struct sockaddr_in local;
#ifdef _WIN32
  int length = sizeof(local);
#else
  socklen_t length = sizeof(local);
#endif
  if (getsockname(udpSocket, (struct sockaddr*) &local, &length) != 0) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushinteger(L, (int) ntohs(local.sin_port));
  return 1;
}

static int l_udpClose(lua_State* L) {
  (void) L;
  udpCloseSocket();
  return 0;
}

static const luaL_Reg udpFunctions[] = {
  { "connect", l_udpConnect },
  { "send", l_udpSend },
  { "receive", l_udpReceive },
  { "port", l_udpPort },
  { "close", l_udpClose },
  { NULL, NULL }
};

int luaopen_udp(lua_State* L) {
  if (!udpSystemInit()) {
    lua_pushnil(L);
    lua_pushstring(L, "não consegui inicializar o sistema de sockets");
    return 2;
  }
  lua_newtable(L);
  luax_register(L, udpFunctions);
  /* Fecha o socket quando o estado Lua termina (jogo fechado). */
  luax_atexit(L, udpCloseSocket);
  luax_atexit(L, udpSystemShutdown);
  return 1;
}

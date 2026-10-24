/**
 * Ícone da bandeja do Loomia (Linux/X11) via StatusNotifierItem.
 *
 * Por que SNI e não XEmbed: em Cinnamon, GNOME e MATE modernos o
 * `_NET_SYSTEM_TRAY_S0` não tem dono (`xprop -root` responde "not found"), então
 * não há para onde reparentar — era por isso que o ícone nunca aparecia.
 * O caminho oficial desses painéis é o StatusNotifierItem (DBus).
 *
 * O binário não depende de libdbus/GTK: fala o protocolo no socket do bus de
 * sessão (AUTH EXTERNAL + marshalling mínimo) e usa Xlib só para o fallback
 * XEmbed (desktops antigos com bandeja clássica).
 *
 *   loomia-tray --title Loomia --tooltip "Cliente Loomia rodando" \
 *               --left-click "loomia --show" [--menu "Rótulo=comando"]...
 *
 * Compilação: cc -O2 -Wall -Wextra loomia-tray.c -o loomia-tray -lX11 -lpthread
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define ICON_SIZE 22
#define MAX_MENU_ITEMS 8
#define MAX_FRAME (1 << 20)

static volatile sig_atomic_t running = 1;

static void on_signal(int signal_number) {
  (void) signal_number;
  running = 0;
}

static void log_line(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
}

static void run_command(const char* command) {
  if (!command || !*command) return;
  log_line("tray: %s", command);
  pid_t pid = fork();
  if (pid == 0) {
    setsid();
    execl("/bin/sh", "sh", "-c", command, (char*) NULL);
    _exit(127);
  }
}

/* ------------------------------------------------------------------------- */
/* Opções                                                                    */
/* ------------------------------------------------------------------------- */

typedef struct {
  char* label;
  char* command;
} MenuItem;

typedef struct {
  const char* title;
  const char* tooltip;
  const char* left_click;
  MenuItem menu[MAX_MENU_ITEMS];
  int menu_count;
} Options;

static Options options;

static void parse_options(int argc, char** argv) {
  options.title = "Loomia";
  options.tooltip = "Cliente Loomia";
  options.left_click = NULL;
  options.menu_count = 0;

  for (int index = 1; index < argc; index++) {
    if (strcmp(argv[index], "--title") == 0 && index + 1 < argc) {
      options.title = argv[++index];
    } else if (strcmp(argv[index], "--tooltip") == 0 && index + 1 < argc) {
      options.tooltip = argv[++index];
    } else if (strcmp(argv[index], "--left-click") == 0 && index + 1 < argc) {
      options.left_click = argv[++index];
    } else if (strcmp(argv[index], "--menu") == 0 && index + 1 < argc) {
      char* spec = strdup(argv[++index]);
      if (!spec) continue;
      char* equals = strchr(spec, '=');
      if (equals && options.menu_count < MAX_MENU_ITEMS) {
        *equals = '\0';
        options.menu[options.menu_count].label = spec;
        options.menu[options.menu_count].command = equals + 1;
        options.menu_count++;
      } else {
        free(spec);
      }
    }
  }
}

/* ------------------------------------------------------------------------- */
/* Ícone ARGB (pré-multiplicado, como o SNI exige)                           */
/* ------------------------------------------------------------------------- */

static unsigned char* make_icon_argb(int size) {
  unsigned char* pixels = calloc((size_t) size * size * 4, 1);
  if (!pixels) return NULL;
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      int border = (x < 2 || y < 2 || x >= size - 2 || y >= size - 2);
      int stem = (x > size / 4 && x < size / 2 && y > 3 && y < size - size / 3);
      int foot = (x > size / 4 && x < size * 3 / 4 && y > size - size / 3 && y < size - 4);
      unsigned char r, g, b, a;
      if (border) {
        r = 234; g = 190; b = 25; a = 255;
      } else if (stem || foot) {
        r = 15; g = 120; b = 190; a = 255;
      } else {
        r = 20; g = 30; b = 48; a = 225;
      }
      pixels[(y * size + x) * 4 + 0] = (unsigned char) (b * a / 255);
      pixels[(y * size + x) * 4 + 1] = (unsigned char) (g * a / 255);
      pixels[(y * size + x) * 4 + 2] = (unsigned char) (r * a / 255);
      pixels[(y * size + x) * 4 + 3] = a;
    }
  }
  return pixels;
}

/* ------------------------------------------------------------------------- */
/* DBus: buffer, tipografia e mensagens                                      */
/* ------------------------------------------------------------------------- */

typedef struct {
  unsigned char* data;
  size_t length;
  size_t capacity;
} Buffer;

static void put_bytes(Buffer* buffer, const void* data, size_t length) {
  if (buffer->length + length > buffer->capacity) {
    size_t capacity = buffer->capacity ? buffer->capacity : 512;
    while (capacity < buffer->length + length) capacity *= 2;
    unsigned char* grown = realloc(buffer->data, capacity);
    if (!grown) return;
    buffer->data = grown;
    buffer->capacity = capacity;
  }
  memcpy(buffer->data + buffer->length, data, length);
  buffer->length += length;
}

static void put_u8(Buffer* buffer, uint8_t value) {
  put_bytes(buffer, &value, 1);
}

static void put_u32(Buffer* buffer, uint32_t value) {
  put_bytes(buffer, &value, 4);
}

static void align_to(Buffer* buffer, size_t alignment) {
  while (buffer->length % alignment) put_u8(buffer, 0);
}

static void put_string(Buffer* buffer, const char* text) {
  uint32_t length = (uint32_t) strlen(text);
  put_u32(buffer, length);
  put_bytes(buffer, text, length);
  put_u8(buffer, 0);
}

static void put_signature(Buffer* buffer, const char* signature) {
  put_u8(buffer, (uint8_t) strlen(signature));
  put_bytes(buffer, signature, strlen(signature));
  put_u8(buffer, 0);
}

static void put_variant_string(Buffer* buffer, const char* value) {
  put_signature(buffer, "s");
  put_string(buffer, value);
}

static void begin_message(Buffer* buffer, uint8_t type, uint8_t flags, uint32_t serial,
                          const char* destination, const char* path, const char* iface,
                          const char* member, const char* signature) {
  memset(buffer, 0, sizeof(*buffer));
  put_u8(buffer, 'l');
  put_u8(buffer, type);
  put_u8(buffer, flags);
  put_u8(buffer, 1);
  put_u32(buffer, 0); /* corpo: preenchido no final */
  put_u32(buffer, serial);

  if (destination && *destination) {
    put_u8(buffer, 6); /* DESTINATION */
    align_to(buffer, 4);
    put_variant_string(buffer, destination);
  }
  if (path) {
    put_u8(buffer, 1); /* PATH */
    align_to(buffer, 4);
    put_variant_string(buffer, path);
  }
  if (iface) {
    put_u8(buffer, 2); /* INTERFACE */
    align_to(buffer, 4);
    put_variant_string(buffer, iface);
  }
  if (member) {
    put_u8(buffer, 3); /* MEMBER */
    align_to(buffer, 4);
    put_variant_string(buffer, member);
  }
  if (signature && *signature) {
    put_u8(buffer, 8); /* SIGNATURE */
    align_to(buffer, 4);
    put_variant_string(buffer, signature);
  }
  align_to(buffer, 8);
}

static void seal_message(Buffer* buffer) {
  uint32_t body = (uint32_t) (buffer->length - 16);
  memcpy(buffer->data + 4, &body, 4);
}

static int send_buffer(int fd, Buffer* buffer) {
  size_t sent = 0;
  while (sent < buffer->length) {
    ssize_t chunk = write(fd, buffer->data + sent, buffer->length - sent);
    if (chunk <= 0) return 0;
    sent += (size_t) chunk;
  }
  return 1;
}

static uint32_t read_u32(const unsigned char* data) {
  uint32_t value;
  memcpy(&value, data, 4);
  return value;
}

/* ------------------------------------------------------------------------- */
/* DBus: conexão e autenticação                                              */
/* ------------------------------------------------------------------------- */

static int connect_bus(void) {
  const char* address = getenv("DBUS_SESSION_BUS_ADDRESS");
  if (!address) return -1;

  char copy[512];
  snprintf(copy, sizeof(copy), "%s", address);
  const char* path = NULL;
  for (char* part = strtok(copy, ";,"); part; part = strtok(NULL, ";,")) {
    if (strncmp(part, "unix:path=", 10) == 0) path = part + 10;
    else if (strncmp(part, "path=", 5) == 0) path = part + 5;
  }
  if (!path) {
    log_line("tray: DBUS_SESSION_BUS_ADDRESS sem unix:path");
    return -1;
  }

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  struct sockaddr_un remote;
  memset(&remote, 0, sizeof(remote));
  remote.sun_family = AF_UNIX;
  snprintf(remote.sun_path, sizeof(remote.sun_path), "%s", path);
  if (connect(fd, (struct sockaddr*) &remote, sizeof(remote)) != 0) {
    close(fd);
    return -1;
  }

  /* O comando AUTH começa com um byte NUL e o UID vai em HEX ASCII
   * (uid 1000 -> "31303030"), não em hexadecimal do número — era o motivo do
   * "REJECTED EXTERNAL" e do ícone nunca aparecer. */
  char uid_text[32];
  int uid_length = snprintf(uid_text, sizeof(uid_text), "%u", (unsigned) getuid());
  char uid_hex[80] = {0};
  for (int index = 0; index < uid_length; index++) {
    snprintf(uid_hex + index * 2, 3, "%02x", (unsigned char) uid_text[index]);
  }
  char text[160];
  int text_length = snprintf(text, sizeof(text), "AUTH EXTERNAL %s\r\n", uid_hex);
  char request[162];
  request[0] = '\0';
  memcpy(request + 1, text, (size_t) text_length);
  if (write(fd, request, (size_t) text_length + 1) != (ssize_t) (text_length + 1)) {
    close(fd);
    return -1;
  }

  char reply[256];
  ssize_t used = read(fd, reply, sizeof(reply) - 1);
  if (used <= 0) {
    close(fd);
    return -1;
  }
  reply[used] = '\0';
  if (!strstr(reply, "OK")) {
    log_line("tray: autenticação no DBus falhou (%s)", reply);
    close(fd);
    return -1;
  }

  char begin[8];
  begin[0] = '\0';
  memcpy(begin + 1, "BEGIN\r\n", 7);
  if (write(fd, begin, 8) != 8) {
    close(fd);
    return -1;
  }
  return fd;
}

/* ------------------------------------------------------------------------- */
/* DBus: mensagens recebidas                                                 */
/* ------------------------------------------------------------------------- */

static size_t header_end(const unsigned char* message) {
  size_t offset = 16;
  size_t body = read_u32(message + 4);
  size_t total = 16 + body;
  while (total % 8) total++;
  (void) offset;
  return total;
}

/** Lê um campo string do cabeçalho. */
static int header_field(const unsigned char* message, uint8_t code, char* out, size_t size) {
  size_t offset = 16;
  size_t end = 16 + read_u32(message + 4);
  while (offset < end) {
    uint8_t field = message[offset++];
    if (field == 0) break;
    while (offset % 8) offset++;
    uint8_t signature_length = message[offset++];
    char signature = (char) message[offset + signature_length];
    offset += (size_t) signature_length + 1;
    if (signature == 's' || signature == 'o' || signature == 'g') {
      while (offset % 4) offset++;
      uint32_t length = read_u32(message + offset);
      offset += 4;
      if (field == code) {
        size_t copy = length < size - 1 ? length : size - 1;
        memcpy(out, message + offset, copy);
        out[copy] = '\0';
        return 1;
      }
      offset += length + 1;
    } else if (signature == 'u') {
      while (offset % 4) offset++;
      offset += 4;
    } else if (signature == 'y') {
      offset += 1;
    } else if (signature == 'b' || signature == 'i') {
      while (offset % 4) offset++;
      offset += 4;
    } else {
      break;
    }
  }
  return 0;
}

/* ------------------------------------------------------------------------- */
/* StatusNotifierItem                                                        */
/* ------------------------------------------------------------------------- */

/** Serial global das mensagens que ENVIAMOS (precisa subir a cada uma). */
static uint32_t our_serial = 1;

/** Retorna uma assinatura de um array de objetos (a{sv}). */
static void put_properties_array_header(Buffer* buffer, uint32_t count) {
  put_signature(buffer, "a{sv}");
  while (buffer->length % 4) put_u8(buffer, 0);
  put_u32(buffer, count);
}

/** Responde GetAll("org.kde.StatusNotifierItem") com o ícone e os textos. */
static void reply_properties(int fd, uint32_t reply_to, const char* destination, unsigned char* pixels) {
  Buffer buffer;
  begin_message(&buffer, 2, 1, our_serial++, destination, "/StatusNotifierItem",
                "org.freedesktop.DBus.Properties", "GetAll", "a{sv}");
  (void) reply_to;

  put_properties_array_header(&buffer, 7);

  put_string(&buffer, "Category");
  put_variant_string(&buffer, "ApplicationStatus");

  put_string(&buffer, "Id");
  put_variant_string(&buffer, "Loomia");

  put_string(&buffer, "Title");
  put_variant_string(&buffer, options.title);

  put_string(&buffer, "Status");
  put_variant_string(&buffer, "Active");

  /* IconName: alguns painéis preferem procurar o nome no tema. */
  put_string(&buffer, "IconName");
  put_variant_string(&buffer, "loomia");

  /* IconPixmap: a(iiay) — w, h, bytes ARGB32 premiultiplicados. */
  put_string(&buffer, "IconPixmap");
  put_signature(&buffer, "a(iiay)");
  while (buffer.length % 4) put_u8(&buffer, 0);
  put_u32(&buffer, 1);
  put_u32(&buffer, ICON_SIZE);
  put_u32(&buffer, ICON_SIZE);
  put_u32(&buffer, ICON_SIZE * ICON_SIZE * 4);
  put_bytes(&buffer, pixels, ICON_SIZE * ICON_SIZE * 4);

  /* ToolTip: (sa(iiay)ss) */
  put_string(&buffer, "ToolTip");
  put_signature(&buffer, "(sa(iiay)ss)");
  while (buffer.length % 4) put_u8(&buffer, 0);
  put_string(&buffer, "loomia");
  put_u32(&buffer, 0); /* sem pixmaps extras */
  put_string(&buffer, options.title);
  put_string(&buffer, options.tooltip);

  seal_message(&buffer);
  send_buffer(fd, &buffer);
  free(buffer.data);
}

/** Responde um `Get(<propriedade>)` individual. */
static void reply_get(int fd, const char* destination, const char* property) {
  Buffer buffer;
  begin_message(&buffer, 2, 1, our_serial++, destination, "/StatusNotifierItem",
                "org.freedesktop.DBus.Properties", "Get", "v");
  if (strcmp(property, "IconPixmap") == 0 || strcmp(property, "ToolTip") == 0) {
    /* O painel sempre usa GetAll antes; aqui basta um valor vazio plausível. */
    put_signature(&buffer, "s");
    put_string(&buffer, "");
  } else {
    put_variant_string(&buffer, strcmp(property, "Id") == 0 ? "Loomia" : options.title);
  }
  seal_message(&buffer);
  send_buffer(fd, &buffer);
  free(buffer.data);
}

/** Responde a uma chamada (Activate/ContextMenu) com um retorno vazio. */
static void reply_empty(int fd, const char* destination, const char* iface, const char* member) {
  Buffer buffer;
  begin_message(&buffer, 2, 1, our_serial++, destination, "/StatusNotifierItem", iface, member, "");
  seal_message(&buffer);
  send_buffer(fd, &buffer);
  free(buffer.data);
}

/** Reserva o nome no bus para que o painel possa chamar este item. */
static int request_name(int fd, const char* service) {
  Buffer buffer;
  begin_message(&buffer, 1, 0, our_serial++, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                "org.freedesktop.DBus", "RequestName", "su");
  put_string(&buffer, service);
  put_u32(&buffer, 0x4 /* DBUS_NAME_FLAG_DO_NOT_QUEUE */);
  seal_message(&buffer);
  int ok = send_buffer(fd, &buffer);
  free(buffer.data);
  return ok;
}

/** Envia `RegisterStatusNotifierItem(service)` ao watcher do painel. */
static int register_sni(int fd, const char* service) {
  Buffer buffer;
  begin_message(&buffer, 1, 0, our_serial++, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem", "s");
  put_string(&buffer, service);
  seal_message(&buffer);
  int ok = send_buffer(fd, &buffer);
  free(buffer.data);
  return ok;
}

/** Lê a propriedade `RegisteredStatusNotifierItems` (diagnóstico). */
static void *bus_thread(void* argument) {
  int fd = (int) (intptr_t) argument;
  static unsigned char frame[MAX_FRAME];
  size_t used = 0;
  unsigned char* pixels = make_icon_argb(ICON_SIZE);
  const char* iface = "org.kde.StatusNotifierItem";

  while (running) {
    if (used + 1 >= sizeof(frame)) {
      log_line("tray: mensagem DBus grande demais (descartada)");
      used = 0;
      continue;
    }
    size_t ask = sizeof(frame) - used - 1;
    ssize_t count = read(fd, frame + used, ask);
    if (count <= 0) break;
    used += (size_t) count;

    size_t offset = 0;
    while (used >= 16) {
      size_t total = header_end(frame + offset);
      if (used < total) break;

      const unsigned char* message = frame + offset;
      uint8_t type = message[1];
      char member[64] = "";
      char sender[128] = "";
      char interface[128] = "";
      header_field(message, 3, member, sizeof(member));
      header_field(message, 7, sender, sizeof(sender));
      header_field(message, 2, interface, sizeof(interface));

      if (type == 1) { /* method call */
        if (strcmp(member, "GetAll") == 0) {
          reply_properties(fd, 0, sender, pixels);
        } else if (strcmp(member, "Get") == 0) {
          reply_get(fd, sender, "Id");
        } else if (strcmp(member, "Activate") == 0) {
          run_command(options.left_click);
          reply_empty(fd, sender, iface, "Activate");
        } else if (strcmp(member, "SecondaryActivate") == 0) {
          run_command(options.left_click);
          reply_empty(fd, sender, iface, "SecondaryActivate");
        } else if (strcmp(member, "ContextMenu") == 0) {
          /* Sem DBusMenu nativo: o menu é oferecido pelo clique esquerdo
           * (o cliente abre a janela, que tem "Fechar Cliente"). */
          reply_empty(fd, sender, iface, "ContextMenu");
        } else if (strcmp(member, "Raise") == 0) {
          run_command(options.left_click);
          reply_empty(fd, sender, iface, "Raise");
        } else if (member[0] != '\0') {
          /* Qualquer outra chamada (Introspect etc.) recebe erro para não
           * travar o painel esperando resposta. */
          Buffer error;
          begin_message(&error, 3, 1, our_serial++, sender, "/StatusNotifierItem",
                        "org.freedesktop.DBus", "Error", "s");
          put_string(&error, "org.freedesktop.DBus.Error.NotSupported");
          seal_message(&error);
          send_buffer(fd, &error);
          free(error.data);
        }
      }

      offset += total;
      used -= total;
    }
    if (offset > 0 && used > 0) memmove(frame, frame + offset, used);
  }

  free(pixels);
  return NULL;
}

/* ------------------------------------------------------------------------- */
/* XEmbed (fallback para bandeja clássica)                                   */
/* ------------------------------------------------------------------------- */

static int x_errors = 0;

static int on_x_error(Display* display, XErrorEvent* error) {
  (void) display;
  (void) error;
  x_errors++;
  return 0;
}

static Window create_xembed_icon(Display* display, int screen, const unsigned char* pixels) {
  char atom_name[64];
  snprintf(atom_name, sizeof(atom_name), "_NET_SYSTEM_TRAY_S%d", screen);
  Window tray = XGetSelectionOwner(display, XInternAtom(display, atom_name, False));
  if (tray == None) return None;

  XSetWindowAttributes attributes;
  attributes.background_pixmap = None;
  attributes.override_redirect = False;
  attributes.event_mask = ButtonReleaseMask | ExposureMask;
  attributes.colormap = DefaultColormap(display, screen);
  attributes.border_pixel = 0;

  Window window = XCreateWindow(display, RootWindow(display, screen), -100, -100, ICON_SIZE, ICON_SIZE, 0,
                                CopyFromParent, InputOutput, CopyFromParent,
                                CWBackPixmap | CWOverrideRedirect | CWEventMask | CWColormap | CWBorderPixel,
                                &attributes);

  GC gc = XCreateGC(display, window, 0, NULL);
  XSetForeground(display, gc, BlackPixel(display, screen));
  XFillRectangle(display, window, gc, 0, 0, ICON_SIZE, ICON_SIZE);
  for (int y = 0; y < ICON_SIZE; y++) {
    for (int x = 0; x < ICON_SIZE; x++) {
      const unsigned char* entry = pixels + (y * ICON_SIZE + x) * 4;
      if (entry[3] < 32) continue;
      unsigned long colour = ((unsigned long) entry[2] << 16) | ((unsigned long) entry[1] << 8) | entry[0];
      XSetForeground(display, gc, colour);
      XDrawPoint(display, window, gc, x, y);
    }
  }
  XFreeGC(display, gc);

  Atom xembed = XInternAtom(display, "_XEMBED", False);
  XEvent message;
  memset(&message, 0, sizeof(message));
  message.xclient.type = ClientMessage;
  message.xclient.window = tray;
  message.xclient.message_type = xembed;
  message.xclient.format = 32;
  message.xclient.data.l[0] = (long) CurrentTime;
  message.xclient.data.l[1] = 0;
  XSendEvent(display, tray, False, NoEventMask, &message);

  x_errors = 0;
  XReparentWindow(display, window, tray, 0, 0);
  XSync(display, False);
  if (x_errors) {
    XDestroyWindow(display, window);
    return None;
  }
  XMapRaised(display, window);
  XSync(display, False);
  return window;
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(int argc, char** argv) {
  parse_options(argc, argv);
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  unsigned char* pixels = make_icon_argb(ICON_SIZE);

  Display* display = XOpenDisplay(NULL);
  if (display) XSetErrorHandler(on_x_error);
  int screen = display ? DefaultScreen(display) : 0;

  char service[64];
  snprintf(service, sizeof(service), "org.kde.StatusNotifierItem.%d", (int) getpid());

  int fd = connect_bus();
  int sni = 0;
  if (fd >= 0) {
    /* Sem reservar o nome o painel não consegue chamar `GetAll` e o ícone
     * nunca aparece (era o caso no Cinnamon). */
    request_name(fd, service);
    sni = register_sni(fd, service);
    if (sni) log_line("tray: StatusNotifierItem registrado (Cinnamon/GNOME/KDE/XFCE)");
    else log_line("tray: watcher do painel ausente");
  }

  Window icon = None;
  if (!sni && display) {
    icon = create_xembed_icon(display, screen, pixels);
    if (icon != None) log_line("tray: ícone XEmbed instalado (bandeja clássica)");
  }

  if (!sni && icon == None) {
    log_line("tray: nenhum painel compatível — cliente segue sem ícone");
  }

  printf("ready\n");
  fflush(stdout);

  pthread_t thread;
  int has_thread = 0;
  if (fd >= 0) {
    if (pthread_create(&thread, NULL, bus_thread, (void*) (intptr_t) fd) == 0) has_thread = 1;
  }

  while (running) {
    if (display && icon && XPending(display) > 0) {
      XEvent event;
      XNextEvent(display, &event);
      if (event.type == ButtonRelease) run_command(options.left_click);
    } else {
      usleep(100000);
    }
    if (display && icon) {
      XWindowAttributes attributes;
      if (!XGetWindowAttributes(display, icon, &attributes) || attributes.map_state == IsUnmapped) {
        XDestroyWindow(display, icon);
        icon = None;
      }
    }
  }

  if (has_thread) pthread_join(thread, NULL);
  if (fd >= 0) close(fd);
  if (display && icon) XDestroyWindow(display, icon);
  free(pixels);
  if (display) XCloseDisplay(display);
  return 0;
}

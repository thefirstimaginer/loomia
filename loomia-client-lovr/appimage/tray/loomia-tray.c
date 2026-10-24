/**
 * Miniaplicativo de bandeja (system tray) do cliente Loomia.
 *
 * Sem GTK, sem libappindicator, sem DBus: cria uma janela pequena com o ícone
 * do Loomia e a reparenta para a bandeja do desktop usando o protocolo XEmbed
 * (_NET_SYSTEM_TRAY_S<screen>), que GNOME (com extensão de bandeja), KDE,
 * XFCE, Cinnamon e MATE entendem.
 *
 *   loomia-tray --title Loomia --tooltip "Cliente Loomia rodando" \
 *               --left-click "loomia-client --show" --menu "Fechar cliente=..."
 *
 * O clique esquerdo executa um comando de shell (a janela do cliente volta ou
 * o cliente é encerrado de vez). Imprime "ready" em stdout quando o ícone
 * está instalado.
 *
 * Build: cc -O2 loomia-tray.c -o loomia-tray -lX11 -lm
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define ICON_SIZE 22
#define MAX_MENU_ITEMS 8

static volatile sig_atomic_t running = 1;

/* Erros de XError (bandeja ausente, reparent recusado) não podem derrubar o
 * cliente: registra no stderr e segue. */
static volatile sig_atomic_t x_failed = 0;

static int on_x_error(Display* display, XErrorEvent* error) {
  (void) display;
  fprintf(stderr, "tray: X error %d (request %d) — seguindo sem bandeja\n",
          error->error_code, error->request_code);
  x_failed = 1;
  return 0;
}

static void on_signal(int signal_number) {
  (void) signal_number;
  running = 0;
}

static void log_line(const char* message) {
  fprintf(stderr, "tray: %s\n", message);
}

static void run_command(const char* command) {
  if (!command || !command[0]) return;
  log_line(command);
  pid_t pid = fork();
  if (pid == 0) {
    setsid();
    execl("/bin/sh", "sh", "-c", command, (char*) NULL);
    _exit(127);
  }
}

/* ------------------------------------------------------------------------- */
/* Ícone: bloco "L" azul com moldura amarela                                 */
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
        r = 20; g = 30; b = 48; a = 235;
      }
      pixels[(y * size + x) * 4 + 0] = b;
      pixels[(y * size + x) * 4 + 1] = g;
      pixels[(y * size + x) * 4 + 2] = r;
      pixels[(y * size + x) * 4 + 3] = a;
    }
  }
  return pixels;
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

static void parse_options(int argc, char** argv, Options* options) {
  options->title = "Loomia";
  options->tooltip = "Cliente Loomia";
  options->left_click = NULL;
  options->menu_count = 0;

  for (int index = 1; index < argc; index++) {
    if (strcmp(argv[index], "--title") == 0 && index + 1 < argc) {
      options->title = argv[++index];
    } else if (strcmp(argv[index], "--tooltip") == 0 && index + 1 < argc) {
      options->tooltip = argv[++index];
    } else if (strcmp(argv[index], "--left-click") == 0 && index + 1 < argc) {
      options->left_click = argv[++index];
    } else if (strcmp(argv[index], "--menu") == 0 && index + 1 < argc) {
      /* "--menu Rótulo=comando" */
      char* spec = strdup(argv[++index]);
      if (!spec) continue;
      char* equals = strchr(spec, '=');
      if (equals && options->menu_count < MAX_MENU_ITEMS) {
        *equals = '\0';
        options->menu[options->menu_count].label = spec;
        options->menu[options->menu_count].command = equals + 1;
        /* O rótulo vive em `spec` (não liberamos de propósito). */
        options->menu_count++;
      } else {
        free(spec);
      }
    }
  }
}

/* ------------------------------------------------------------------------- */
/* XEmbed                                                                    */
/* ------------------------------------------------------------------------- */

static Window create_icon_window(Display* display, int screen, unsigned char* pixels) {
  Window root = RootWindow(display, screen);
  XSetWindowAttributes attributes;
  attributes.background_pixmap = None;
  attributes.override_redirect = False;
  attributes.event_mask = ButtonPressMask | ButtonReleaseMask | ExposureMask;

  Window window = XCreateWindow(display, root, -100, -100, ICON_SIZE, ICON_SIZE, 0,
                                CopyFromParent, InputOutput, CopyFromParent,
                                CWOverrideRedirect | CWBackPixmap | CWEventMask, &attributes);

  /* Sem XImage/storage persistente: pinta pixel a pixel (o buffer `pixels`
   * é liberado no fim e o servidor X não fica com ponteiro inválido). */
  GC gc = XCreateGC(display, window, 0, NULL);
  unsigned long pixel = BlackPixel(display, screen);
  XSetForeground(display, gc, pixel);
  XFillRectangle(display, window, gc, 0, 0, ICON_SIZE, ICON_SIZE);
  for (int y = 0; y < ICON_SIZE; y++) {
    for (int x = 0; x < ICON_SIZE; x++) {
      unsigned char* entry = pixels + (y * ICON_SIZE + x) * 4;
      if (entry[3] < 32) continue;
      unsigned long colour = ((unsigned long) entry[2] << 16) |
                             ((unsigned long) entry[1] << 8) | entry[0];
      XSetForeground(display, gc, colour);
      XDrawPoint(display, window, gc, x, y);
    }
  }
  XFreeGC(display, gc);

  /* Reparenta para o dono de _NET_SYSTEM_TRAY_S<screen> (bandeja). */
  char atom_name[64];
  snprintf(atom_name, sizeof(atom_name), "_NET_SYSTEM_TRAY_S%d", screen);
  Atom tray_atom = XInternAtom(display, atom_name, False);
  Window tray = XGetSelectionOwner(display, tray_atom);
  if (tray == None) {
    /* Sem dono não existe bandeja: NÃO deixar a janela solta na tela (era o
     * que aparecia como uma "mini janela" flutuante sem título). Destrói e o
     * cliente segue sem ícone. */
    fprintf(stderr, "tray: este desktop não tem bandeja XEmbed\n");
    XDestroyWindow(display, window);
    return None;
  }

  Atom xembed = XInternAtom(display, "_XEMBED", False);
  XEvent event;
  memset(&event, 0, sizeof(event));
  event.xclient.type = ClientMessage;
  event.xclient.window = tray;
  event.xclient.message_type = xembed;
  event.xclient.format = 32;
  event.xclient.data.l[0] = (long) CurrentTime;
  event.xclient.data.l[1] = 0; /* XEMBED_EMBEDDED_NOTIFY */
  XSendEvent(display, tray, False, NoEventMask, &event);
  /* Erros do X não podem derrubar o cliente: bandeja ausente/incompatível é
   * só um aviso (o log registra e a janela continua viva). */
  x_failed = 0;
  XReparentWindow(display, window, tray, 0, 0);
  XSync(display, False);
  if (x_failed) {
    /* O reparent foi recusado: destruir evita a "mini janela" solta. */
    fprintf(stderr, "tray: bandeja recusou o ícone\n");
    XDestroyWindow(display, window);
    return None;
  }
  /* Só mapeia depois do reparent dar certo: a janela nasce dentro da bandeja
   * (mapear antes fazia o clique cair no ícone errado). */
  XMapRaised(display, window);
  XSync(display, False);
  return window;
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(int argc, char** argv) {
  Options options;
  parse_options(argc, argv, &options);

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  XSetErrorHandler(on_x_error);

  Display* display = XOpenDisplay(NULL);
  if (!display) {
    log_line("sem servidor X — cliente segue sem ícone na bandeja");
    printf("ready\n");
    fflush(stdout);
    while (running) sleep(1);
    return 0;
  }

  unsigned char* pixels = make_icon_argb(ICON_SIZE);
  int screen = DefaultScreen(display);
  Window icon = create_icon_window(display, screen, pixels);

  printf("ready\n");
  fflush(stdout);
  fprintf(stderr, "tray: ícone instalado (%s)\n", options.tooltip);

  time_t last_attempt = 0;
  while (running) {
    if (!icon) {
      /* Sem bandeja: tenta de novo a cada 5 s (mas nunca deixa janela solta). */
      time_t now = time(NULL);
      if (now - last_attempt >= 5) {
        last_attempt = now;
        icon = create_icon_window(display, screen, pixels);
      }
    }

    while (XPending(display) > 0) {
      XEvent event;
      XNextEvent(display, &event);
      if (event.type == ButtonRelease) {
        run_command(options.left_click);
      }
    }

    /* A bandeja pode reiniciar e soltar a janela: recria se sumiu. */
    XWindowAttributes attributes;
    if (icon && (!XGetWindowAttributes(display, icon, &attributes) || attributes.map_state == IsUnmapped)) {
      XDestroyWindow(display, icon);
      icon = None;
      last_attempt = time(NULL);
    }
    usleep(100000);
  }

  if (icon) XDestroyWindow(display, icon);
  XFree(pixels);

  XCloseDisplay(display);
  return 0;
}

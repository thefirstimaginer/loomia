/* Stub mínimo do Xinerama para builds sem libxinerama-dev.
 *
 * O GLFW 3.4 carrega o libXinerama em tempo de execução (dlopen) e só usa os
 * protótipos no código; o header real deste sistema só não está instalado.
 * Se a biblioteca existir no runtime, o suporte a multi-monitor funciona
 * normalmente; se não existir, o GLFW segue sem o Xinerama.
 */
#ifndef XINERAMA_STUB_H
#define XINERAMA_STUB_H

#include <X11/Xlib.h>

typedef struct {
  int screen_number;
  short x_org;
  short y_org;
  short width;
  short height;
} XineramaScreenInfo;

Bool XineramaQueryExtension(Display* display, int* event_base, int* error_base);
Status XineramaQueryVersion(Display* display, int* major, int* minor);
Bool XineramaIsActive(Display* display);
XineramaScreenInfo* XineramaQueryScreens(Display* display, int* number);

#endif

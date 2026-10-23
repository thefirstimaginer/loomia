/* Stub de X11/Xlib-xcb.h (pacote libx11-xcb-dev ausente).
 *
 * O Loomia Engine usa só `Display`/`Window` do Xlib; o Xlib-xcb existe apenas
 * para o GLFW expor handles nativos. Este header puxa o Xlib real e declara o
 * `xcb_connection_t` opaco, suficiente para compilar.
 */
#ifndef LOOMIA_XLIB_XCB_STUB_H
#define LOOMIA_XLIB_XCB_STUB_H

#include <X11/Xlib.h>

typedef struct xcb_connection_t xcb_connection_t;

#endif

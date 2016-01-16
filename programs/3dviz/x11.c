#include <err.h>
#include <stdlib.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <GL/glew.h>
#include <GL/glxew.h>
#include <GL/glx.h>

#include "x11.h"

static int x11_gotMapNotify;

Display*     X11_display = 0;
Window       X11_window;
XVisualInfo* X11_visual;
XIM          X11_xim;
XIC          X11_xic;
GLXContext   X11_glxContext;

unsigned int X11_window_width = 800;
unsigned int X11_window_height = 600;

Atom X11_xa_wm_delete_window;

static void
x11_ProcessEvent (XEvent *event);

void
X11_Init (void)
{
  int attributes[] =
    {
      GLX_RGBA,
      GLX_DEPTH_SIZE, 16,
      GLX_RED_SIZE, 8,
      GLX_GREEN_SIZE, 8,
      GLX_BLUE_SIZE, 8,
      GLX_DOUBLEBUFFER,
      None
    };

  Colormap color_map;
  XSetWindowAttributes attr;
  XEvent event;
  char* p;

  XInitThreads ();

  X11_display = XOpenDisplay (0);

  if (!X11_display)
    {
      const char* displayName = getenv ("DISPLAY");

      errx (EXIT_FAILURE, "Failed to open X11_display %s", displayName ? displayName : ":0");
    }

  XSynchronize (X11_display, True);

  if (!glXQueryExtension (X11_display, 0, 0))
    errx (EXIT_FAILURE, "No GLX extension present");

  if (!(X11_visual = glXChooseVisual (X11_display, DefaultScreen (X11_display), attributes)))
    errx (EXIT_FAILURE, "glXChooseVisual failed");

  if (!(X11_glxContext = glXCreateContext (X11_display, X11_visual, 0, GL_TRUE)))
    errx (EXIT_FAILURE, "Failed creating OpenGL context");

  color_map = XCreateColormap (X11_display,
                               RootWindow (X11_display, X11_visual->screen),
                               X11_visual->visual, AllocNone);

  attr.colormap = color_map;
  attr.border_pixel = 0;
  attr.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask | ExposureMask | FocusChangeMask;

  X11_window = XCreateWindow (X11_display, RootWindow (X11_display, X11_visual->screen),
                         0, 0, X11_window_width, X11_window_height,
                         0, X11_visual->depth, InputOutput, X11_visual->visual,
                         CWBorderPixel | CWColormap | CWEventMask,
                         &attr);

  XMapRaised (X11_display, X11_window);

  if ((p = XSetLocaleModifiers ("")) && *p)
    X11_xim = XOpenIM (X11_display, 0, 0, 0);

  if (!X11_xim && (p = XSetLocaleModifiers ("@im=none")) && *p)
    X11_xim = XOpenIM (X11_display, 0, 0, 0);

  if (!X11_xim)
    errx (EXIT_FAILURE, "Failed to open X Input Method");

  X11_xic = XCreateIC (X11_xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                   XNClientWindow, X11_window, XNFocusWindow, X11_window, NULL);

  if (!X11_xic)
    errx (EXIT_FAILURE, "Failed to create X Input Context");

  while (!x11_gotMapNotify)
    {
      XEvent event;

      XNextEvent (X11_display, &event);

      x11_ProcessEvent (&event);
    }

  if (!glXMakeCurrent (X11_display, X11_window, X11_glxContext))
    errx (EXIT_FAILURE, "glXMakeCurrent returned false");

  glewInit ();

  X11_xa_wm_delete_window = XInternAtom (X11_display, "WM_DELETE_WINDOW", False);

  XSynchronize (X11_display, False);
}

static void
x11_ProcessEvent (XEvent *event)
{
  switch (event->type)
    {
    case MapNotify:

      if (event->xmap.window == X11_window)
        x11_gotMapNotify = 1;

      break;

    case ConfigureNotify:

      /* Skip to last ConfigureNotify event */
      while (XCheckTypedWindowEvent (X11_display, X11_window, ConfigureNotify, event))
        {
          /* Do nothing */
        }

      glViewport (0, 0, X11_window_width, X11_window_height);

      X11_window_width = event->xconfigure.width;
      X11_window_height = event->xconfigure.height;

      break;

    case ClientMessage:

      if (event->xclient.data.l[0] == X11_xa_wm_delete_window)
        exit (EXIT_SUCCESS);

      break;
    }
}

void
X11_ProcessEvents (void)
{
  XEvent event;

  while (XPending (X11_display))
    {
      XNextEvent (X11_display, &event);

      x11_ProcessEvent (&event);
    }
}

void
X11_SwapBuffers (void)
{
  glXSwapBuffers (X11_display, X11_window);
}

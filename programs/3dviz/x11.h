#ifndef X11_H_
#define X11_H_ 1

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned int X11_window_width;
extern unsigned int X11_window_height;

void
X11_Init (void);

void
X11_ProcessEvents (void);

void
X11_SwapBuffers(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif /* !X11_H_ */

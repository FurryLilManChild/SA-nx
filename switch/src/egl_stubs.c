/* SA-nx: devkitPro's SDL2 is linked for audio only. Its video code still references EGL, and
 * linking switch-mesa's real EGL would pull Mesa's GL stack in next to NVK. SDL's video subsystem
 * is never initialised here, so every entry point just fails. */
#include <stddef.h>

typedef void* EGLany;
typedef unsigned int EGLBoolean;
typedef int EGLint;

#define FAIL_PTR(name) EGLany name() { return NULL; }
#define FAIL_BOOL(name) EGLBoolean name() { return 0; }

FAIL_PTR(eglGetDisplay)
FAIL_PTR(eglGetPlatformDisplay)
FAIL_PTR(eglGetProcAddress)
FAIL_PTR(eglCreateContext)
FAIL_PTR(eglCreatePbufferSurface)
FAIL_PTR(eglCreateWindowSurface)
FAIL_PTR(eglQueryString)
FAIL_BOOL(eglInitialize)
FAIL_BOOL(eglTerminate)
FAIL_BOOL(eglChooseConfig)
FAIL_BOOL(eglGetConfigAttrib)
FAIL_BOOL(eglDestroyContext)
FAIL_BOOL(eglDestroySurface)
FAIL_BOOL(eglMakeCurrent)
FAIL_BOOL(eglSwapBuffers)
FAIL_BOOL(eglSwapInterval)
FAIL_BOOL(eglWaitNative)
FAIL_BOOL(eglWaitGL)
FAIL_BOOL(eglBindAPI)
unsigned int eglQueryAPI(void) { return 0; }
EGLint eglGetError(void) { return 0x3001; /* EGL_NOT_INITIALIZED */ }

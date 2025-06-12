#define SOKOL_D3D11
#define SOKOL_GFX_IMPL
#define XHL_ALLOC_IMPL
#define XHL_FILES_IMPL
#define XHL_MATHS_IMPL
#define XHL_THREAD_IMPL
#define XHL_TIME_IMPL
#define STB_IMAGE_IMPLEMENTATION

#ifndef NDEBUG
#define SOKOL_ASSERT(cond) (cond) ? (void)0 : __debugbreak()
#endif

#include "common.h"

#include <cplug_extensions/window_win.c>
#include <sokol_gfx.h>
#include <stb_image.h>

#include <stdio.h>
#include <xhl/alloc.h>
#include <xhl/debug.h>
#include <xhl/files.h>
#include <xhl/maths.h>
#include <xhl/thread.h>
#include <xhl/time.h>

#ifndef NDEBUG
void println(const char* const fmt, ...)
{
    char    buf[256] = {0};
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0)
    {
        if (n < sizeof(buf) && buf[n - 1] != '\n')
        {
            buf[n] = '\n';
            n++;
        }

#ifdef CPLUG_BUILD_STANDALONE
        OutputDebugStringA(buf);
        // fwrite(buf, 1, n, stderr);
#else
        // char path[1024];
        // bool ok = xfiles_get_user_directory(path, sizeof(path), XFILES_USER_DIRECTORY_DESKTOP);
        // xassert(ok);
        // strcat(path, "\\log.txt");
        // ok = ok && xfiles_append(path, buf, n);
        // xassert(ok);
#endif
    }
}
#endif // NDEBUG

void library_load_platform()
{
}

void library_unload_platform()
{
}
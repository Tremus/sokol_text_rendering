#define XHL_ALLOC_IMPL
#define XHL_FILES_IMPL
#define XHL_MATHS_IMPL
#define XHL_THREAD_IMPL
#define XHL_TIME_IMPL

#include "common.h"

#include <stdarg.h>
#include <stdio.h>
#include <xhl/alloc.h>
#include <xhl/debug.h>
#include <xhl/files.h>
#include <xhl/maths.h>
#include <xhl/thread.h>
#include <xhl/time.h>

CFRunLoopTimerRef g_timer;
int               g_platform_init_counter = 0;

#ifndef NDEBUG
void println(const char* const fmt, ...)
{
    char    buf[256] = {0};
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n)
    {
        if (n < sizeof(buf) && buf[n - 1] != '\n')
        {
            buf[n] = '\n';
            n++;
        }

#ifdef CPLUG_BUILD_STANDALONE
        fwrite(buf, 1, n, stdout);
#else
        // static char path[1024] = {0};
        // if (strlen(path) == 0)
        // {
        //     bool ok = xfiles_get_user_directory(path, sizeof(path), XFILES_USER_DIRECTORY_DESKTOP);
        //     xassert(ok);
        //     strcat(path, "/log.txt");
        // }
        // bool ok = xfiles_append(path, buf, n);
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

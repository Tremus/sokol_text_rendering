#pragma once
#include "common.h"
#include <cplug.h>
#include <xhl/thread.h>

typedef struct Plugin
{
    CplugHostContext* cplug_ctx;

    // Retained data for GUI
    void* gui;
    int   width, height;

    // Plugin data
    double   sample_rate;
    uint32_t max_block_size;
} Plugin;

bool is_main_thread();

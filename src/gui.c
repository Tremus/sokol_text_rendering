#include "common.h"
#include "plugin.h"

#include <xhl/array.h>
#include <xhl/debug.h>
#include <xhl/files.h>
#include <xhl/maths.h>
#include <xhl/time.h>
#include <xhl/vector.h>

#include <cplug_extensions/window.h>
#include <sokol_gfx.h>
#include <stb_image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text_rendering_layer.h"

#include <img.glsl.h>

// TODO
// Bind correct atlas img when drawing text
// Handle variety of text alignments. eg TL, TC, TR, CL, CC, CR, BL, BC, BR
// Handle proper blending of text so glyphs don't clip each other
// Handle proper blending of text so subpixel antialiasing blends with background
// Handle multiple fonts (bold/italic) & font sizes
// Test mixed language strings (Try this? https://github.com/Tehreer/SheenBidi)
// Add ability to clear font atlas on resize
// Use smaller atlases 128x128 (RGBA 64kb)

// https://utf8everywhere.org/
static const char* MY_TEXT = "abc";
// static const char* MY_TEXT = "Sphinx of black quartz, judge my vow";
// static const char* MY_TEXT = "AV. .W.V.";
// This used to display correctly in my IDE (VSCode) but it appears to be broken. kb_text_shape v1 couldn't properly
// segment the text and struggled to correctly position the hebrew glyphs. v2.0 appears to be perfect! Hoorah
// static const char* MY_TEXT = "Приве́т नमस्ते שָׁלוֹם  wow 🐨";
// static const char* MY_TEXT = "UTF8 Приве́т नमस्ते שָׁלוֹם";
// static const char* MY_TEXT = "שָׁלוֹם";
// static const char* MY_TEXT = "UTF8 Приве́т";
// NOTE: in order to correctly shape this text with kbts, you must explicitly say the text is LTR direction
// static const char* MY_TEXT = "-48.37dB + 10";

enum
{
    // Sans-serif style typefaces become hard to read below a font size of 7px
    // 8px should be the minimum
    FONT_SIZE = 12,
};

struct load_img_t
{
    sg_image img;
    sg_view  view;
    int      width, height;
};

bool load_image(const char* path, struct load_img_t* out)
{
    void*  file_data     = NULL;
    size_t file_data_len = 0;
    bool   ok            = false;

    ok = xfiles_read(path, &file_data, &file_data_len);
    xassert(ok);

    if (ok)
    {
        // stbi_set_flip_vertically_on_load(1);

        int      x = 0, y = 0, comp = 0;
        stbi_uc* img_buf = stbi_load_from_memory(file_data, file_data_len, &x, &y, &comp, 4);
        xassert(img_buf);
        ok = img_buf != NULL;
        if (ok)
        {
            out->img = sg_make_image(&(sg_image_desc){
                .width        = x,
                .height       = y,
                .pixel_format = SG_PIXELFORMAT_RGBA8,

                .data.mip_levels[0] = {
                    .ptr  = img_buf,
                    .size = x * y * 4,
                }});
            stbi_image_free(img_buf);

            out->view = sg_make_view(&(sg_view_desc){.texture = out->img});

            out->width  = x;
            out->height = x;
        }

        XFILES_FREE(file_data);
    }
    return ok;
}

typedef struct GUI
{
    Plugin*      plugin;
    void*        pw;
    _sg_state_t* sg;

    sg_pipeline       img_pip;
    struct load_img_t img_girl;
    sg_sampler        sampler_linear;
    sg_sampler        sampler_nearest;

    // struct load_img_t brain;

    TextLayer* tl;
} GUI;

static void my_sg_logger(
    const char* tag,              // always "sapp"
    uint32_t    log_level,        // 0=panic, 1=error, 2=warning, 3=info
    uint32_t    log_item_id,      // SAPP_LOGITEM_*
    const char* message_or_null,  // a message string, may be nullptr in release mode
    uint32_t    line_nr,          // line number in sokol_app.h
    const char* filename_or_null, // source filename, may be nullptr in release mode
    void*       user_data)
{
    static char* LOG_LEVEL[] = {
        "PANIC",
        "ERROR",
        "WARNING",
        "INFO",
    };
    CPLUG_LOG_ASSERT(log_level > 1 && log_level < ARRLEN(LOG_LEVEL));
    if (!message_or_null)
        message_or_null = "";
    println("[%s] %s %u:%s", LOG_LEVEL[log_level], message_or_null, line_nr, filename_or_null);
}

void* my_sg_allocator_alloc(size_t size, void* user_data)
{
    void* ptr = MY_MALLOC(size);
    return ptr;
}
void my_sg_allocator_free(void* ptr, void* user_data) { MY_FREE(ptr); }

void pw_get_info(struct PWGetInfo* info)
{
    if (info->type == PW_INFO_INIT_SIZE)
    {
        Plugin* p              = info->init_size.plugin;
        info->init_size.width  = p->width;
        info->init_size.height = p->height;
    }
    else if (info->type == PW_INFO_CONSTRAIN_SIZE)
    {
        uint32_t width  = info->constrain_size.width;
        uint32_t height = info->constrain_size.height;

        if (width < GUI_MIN_WIDTH)
            width = GUI_MIN_WIDTH;
        if (height < GUI_MIN_HEIGHT)
            height = GUI_MIN_HEIGHT;

        info->constrain_size.width  = width;
        info->constrain_size.height = height;
    }
}

void* pw_create_gui(void* _plugin, void* _pw)
{
    CPLUG_LOG_ASSERT(_plugin);
    CPLUG_LOG_ASSERT(_pw);
    Plugin* p   = _plugin;
    GUI*    gui = MY_CALLOC(1, sizeof(*gui));
    gui->plugin = p;
    gui->pw     = _pw;
    p->gui      = gui;

    sg_environment env;
    memset(&env, 0, sizeof(env));
    env.defaults.sample_count = 1;
    env.defaults.color_format = SG_PIXELFORMAT_BGRA8;
    env.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
#if __APPLE__
    env.metal.device = pw_get_metal_device(gui->pw);
#elif _WIN32
    env.d3d11.device         = pw_get_dx11_device(gui->pw);
    env.d3d11.device_context = pw_get_dx11_device_context(gui->pw);
#endif
    gui->sg = sg_setup(&(sg_desc){
        .allocator =
            {
                .alloc_fn  = my_sg_allocator_alloc,
                .free_fn   = my_sg_allocator_free,
                .user_data = gui,
            },
        .environment        = env,
        .logger             = my_sg_logger,
        .pipeline_pool_size = 512,
    });

#if defined(_WIN32)
    // const char* font_path = "C:\\Windows\\Fonts\\arialbd.ttf";
    const char* font_path = "C:\\Windows\\Fonts\\seguisb.ttf";
    // const char* font_path = "C:\\Windows\\Fonts\\segoeui.ttf";
    // const char* font_path = "C:\\Windows\\Fonts\\segoeuib.ttf"; // bold
    // const char* font_path = "C:\\Windows\\Fonts\\seguisb.ttf"; // semibold
    // const char* font_path = SRC_DIR XFILES_DIR_STR "assets" XFILES_DIR_STR "EBGaramond-Regular.ttf";
    // const char* font_path = SRC_DIR XFILES_DIR_STR "assets" XFILES_DIR_STR "NotoSansHebrew-Regular.ttf";
#elif defined(__APPLE__)
    const char* font_path = "/Library/Fonts/Arial Unicode.ttf";
#endif
    xassert(xfiles_exists(font_path));

    gui->tl = text_layer_new(font_path);
    // text_layer_prerender_ascii(gui->tl, FONT_SIZE);

    gui->img_pip         = sg_make_pipeline(&(sg_pipeline_desc){
                .shader = sg_make_shader(texread_shader_desc(sg_query_backend())),
    });
    gui->sampler_linear  = sg_make_sampler(&(sg_sampler_desc){
         .min_filter    = SG_FILTER_LINEAR,
         .mag_filter    = SG_FILTER_LINEAR,
         .mipmap_filter = SG_FILTER_LINEAR,
         .wrap_u        = SG_WRAP_CLAMP_TO_EDGE,
         .wrap_v        = SG_WRAP_CLAMP_TO_EDGE,
    });
    gui->sampler_nearest = sg_make_sampler(&(sg_sampler_desc){
        .min_filter    = SG_FILTER_NEAREST,
        .mag_filter    = SG_FILTER_NEAREST,
        .mipmap_filter = SG_FILTER_NEAREST,
        .wrap_u        = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v        = SG_WRAP_CLAMP_TO_EDGE,
    });

    // Load image
    // src=https://www.w3schools.com/tags//tryit.asp?filename=tryhtml_image_test
    const char* img_path = SRC_DIR XFILES_DIR_STR "assets" XFILES_DIR_STR "img_girl.jpg";
    bool        ok       = load_image(img_path, &gui->img_girl);

    return gui;
}

void pw_destroy_gui(void* _gui)
{
    GUI* gui = _gui;

    text_layer_destroy(gui->tl);

    sg_set_global(gui->sg);
    sg_shutdown(gui->sg);

    gui->plugin->gui = NULL;
    MY_FREE(gui);
}

bool pw_event(const PWEvent* event)
{
    GUI* gui = event->gui;

    if (!gui || !gui->plugin)
        return false;

    if (event->type == PW_EVENT_RESIZE_UPDATE)
    {
        // Retain size info for when the GUI is destroyed / reopened
        gui->plugin->width  = event->resize.width;
        gui->plugin->height = event->resize.height;
    }

    return false;
}

void pw_tick(void* _gui)
{
    GUI* gui = _gui;
    CPLUG_LOG_ASSERT(gui->plugin);
    if (!gui || !gui->plugin)
        return;

    const int   gui_width  = gui->plugin->width;
    const int   gui_height = gui->plugin->height;
    const float dpi        = pw_get_dpi(gui->pw);

    // Begin frame
    {
        sg_pass_action pass_action = {
            .colors[0] = {.load_action = SG_LOADACTION_CLEAR, .clear_value = {0, 0, 0, 1.0f}}};

        sg_swapchain swapchain;
        memset(&swapchain, 0, sizeof(swapchain));
        swapchain.width        = gui_width;
        swapchain.height       = gui_height;
        swapchain.sample_count = 1;
        swapchain.color_format = SG_PIXELFORMAT_BGRA8;
        swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;

#if __APPLE__
        swapchain.metal.current_drawable      = pw_get_metal_drawable(gui->pw);
        swapchain.metal.depth_stencil_texture = pw_get_metal_depth_stencil_texture(gui->pw);
#endif
#if _WIN32
        swapchain.d3d11.render_view        = pw_get_dx11_render_target_view(gui->pw);
        swapchain.d3d11.depth_stencil_view = pw_get_dx11_depth_stencil_view(gui->pw);
#endif
        sg_set_global(gui->sg);
        sg_begin_pass(&(sg_pass){.action = pass_action, .swapchain = swapchain});
    }

    // bool draw_background_image = true;
    bool draw_background_image = false;
    if (draw_background_image)
    {
        sg_apply_pipeline(gui->img_pip);
        sg_apply_bindings(&(sg_bindings){
            .views[VIEW_tex]   = gui->img_girl.view,
            .samplers[SMP_smp] = gui->sampler_nearest,
        });
        sg_draw(0, 3, 1);
    }

    // Move pen to centre of gui
    // pen_x += 10;             // padding
    int pen_x = gui_width / 2;                      // Horizontal centre
    int pen_y = (gui_height / 2) - (FONT_SIZE / 2); // Vertical centre

    text_layer_draw_text(gui->tl, MY_TEXT, NULL, pen_x, pen_y, FONT_SIZE);
    text_layer_draw(gui->tl, gui->sampler_nearest, gui_width, gui_height);

    sg_end_pass();
    sg_commit();
    sg_set_global(NULL);
}

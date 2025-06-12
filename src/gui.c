#define STB_RECT_PACK_IMPLEMENTATION

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
#include <stb_rect_pack.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <ft2build.h>
#include <hb.h>

#include <hb-ft.h>

#include <text.glsl.h>

// TODO
// Do layout with harfbuzz
// Handle proper blending of text
// Handle multiple fonts (bold/italic) & font sizes
// If atlas is full, create new font atlas
// Add ability to clear font atlas on resize
// Use smaller atlases 128x128 (RGBA 64kb)

struct load_img_t
{
    sg_image img;
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
        stbi_set_flip_vertically_on_load(1);

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

                .data.subimage[0][0] = {
                    .ptr  = img_buf,
                    .size = x * y * 4,
                }});
            stbi_image_free(img_buf);

            out->width  = x;
            out->height = x;
        }

        XFILES_FREE(file_data);
    }
    return ok;
}

typedef struct
{
    float   x, y;
    int16_t u, v;
} vertex_t;

typedef struct GUI
{
    Plugin*      plugin;
    void*        pw;
    _sg_state_t* sg;

    stbrp_context font_atlas;
    stbrp_node*   nodes;
    stbrp_rect*   rects;

    FT_Library ft_lib;
    FT_Face    ft_face;

    sg_pipeline pip;
    sg_buffer   vbo;
    sg_buffer   ibo;
    sg_image    img;
    sg_sampler  smp;

    // struct load_img_t brain;
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

    int err = FT_Init_FreeType(&gui->ft_lib);
    xassert(!err);
    err = FT_New_Face(gui->ft_lib, "C:\\Windows\\Fonts\\arialbd.ttf", 0, &gui->ft_face);
    xassert(!err);
    println("Found %ld glyphs", gui->ft_face->num_glyphs);

    const float FONT_SIZE = 12;
    const float DPI       = 96;
    FT_Set_Char_Size(gui->ft_face, 0, FONT_SIZE * 64, DPI, DPI);

    const int      ATLAS_WIDTH      = 128;
    const int      ATLAS_HEIGHT     = ATLAS_WIDTH;
    const int      ATLAS_ROW_STRIDE = ATLAS_WIDTH * 4;
    unsigned char* atlas            = xcalloc(1, ATLAS_HEIGHT * ATLAS_ROW_STRIDE);

    xarr_setlen(gui->nodes, (ATLAS_WIDTH * 2));
    stbrp_init_target(&gui->font_atlas, ATLAS_WIDTH, ATLAS_HEIGHT, gui->nodes, xarr_len(gui->nodes));

    // from "!" to "~" https://www.ascii-code.com/
    for (int codepoint = 33; codepoint < 127; codepoint++)
    {
        FT_UInt glyph_index = FT_Get_Char_Index(gui->ft_face, codepoint);
        err                 = FT_Load_Glyph(gui->ft_face, glyph_index, FT_LOAD_DEFAULT);
        xassert(!err);

        FT_Render_Mode render_mode = FT_RENDER_MODE_LCD; // subpixel antialiasing, horizontal screen
        FT_Render_Glyph(gui->ft_face->glyph, render_mode);

        const FT_Bitmap* bmp = &gui->ft_face->glyph->bitmap;
        xassert(bmp->pixel_mode == FT_PIXEL_MODE_LCD);
        xassert((bmp->width % 3) == 0); // note: FT width is measured in bytes (subpixels)

        // Note all glyphs have height/rows... (spaces?)
        if (bmp->width && bmp->rows)
        {
            int        width_pixels = bmp->width / 3;
            stbrp_rect rect         = {
                        .id = glyph_index,
                        .w  = width_pixels + 1,
                        .h  = bmp->rows + 1,
            };
            int num_packed = stbrp_pack_rects(&gui->font_atlas, &rect, 1);
            xassert(num_packed);
            if (num_packed)
            {
                xarr_push(gui->rects, rect);

                println("Printing character \"%c\" to atlas at %dx%d", (char)codepoint, rect.x, rect.y);

                for (int y = 0; y < bmp->rows; y++)
                {
                    unsigned char* dst = atlas + (rect.y + y) * ATLAS_ROW_STRIDE + rect.x * 4;
                    unsigned char* src = bmp->buffer + y * bmp->pitch;

                    for (int x = 0; x < width_pixels; x++, dst += 4, src += 3)
                    {
                        dst[0] = src[0];
                        dst[1] = src[1];
                        dst[2] = src[2];
                        dst[3] = 0xff;
                    }
                }
            }
        }
    }

    // img shader
    {
        static const uint16_t indices[] = {0, 1, 2, 0, 2, 3};

        gui->vbo = sg_make_buffer(&(sg_buffer_desc){
            .usage.vertex_buffer = true,
            .usage.stream_update = true,
            .size                = sizeof(vertex_t) * 4,
            .label               = "img-vertices"});

        gui->ibo = sg_make_buffer(
            &(sg_buffer_desc){.usage.index_buffer = true, .data = SG_RANGE(indices), .label = "img-indices"});

        sg_shader shd = sg_make_shader(text_shader_desc(sg_query_backend()));
        gui->pip      = sg_make_pipeline(&(sg_pipeline_desc){
                 .shader     = shd,
                 .index_type = SG_INDEXTYPE_UINT16,
                 .layout =
                {.attrs =
                          {[ATTR_text_position].format  = SG_VERTEXFORMAT_FLOAT2,
                           [ATTR_text_texcoord0].format = SG_VERTEXFORMAT_SHORT2N}},
                 .colors[0] =
                {.write_mask = SG_COLORMASK_RGBA,
                      .blend =
                          {
                              .enabled          = true,
                              .src_factor_rgb   = SG_BLENDFACTOR_ONE,
                              .src_factor_alpha = SG_BLENDFACTOR_ONE,
                              .dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                              .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                     }},
                 .label = "img-pipeline"});

        // a sampler object
        gui->smp = sg_make_sampler(&(sg_sampler_desc){
            .min_filter = SG_FILTER_NEAREST,
            .mag_filter = SG_FILTER_NEAREST,
            .wrap_u     = SG_WRAP_CLAMP_TO_EDGE,
            .wrap_v     = SG_WRAP_CLAMP_TO_EDGE,
        });

        gui->img = sg_make_image(&(sg_image_desc){
            .width  = ATLAS_WIDTH,
            .height = ATLAS_HEIGHT,
            // .num_slices   = 1,
            .pixel_format = SG_PIXELFORMAT_RGBA8,

            .data.subimage[0][0] = {
                .ptr  = atlas,
                .size = ATLAS_HEIGHT * ATLAS_ROW_STRIDE,
            }});
    }

    xfree(atlas);

    // img
    // {
    //     char path[1024];
    //     xfiles_get_user_directory(path, sizeof(path), XFILES_USER_DIRECTORY_DESKTOP);
    //     int         len = strlen(path);
    //     const char* cat = XFILES_DIR_STR "brain.jpg";
    //     snprintf(path + len, sizeof(path) - len, "%s", cat);

    //     bool ok = load_image(path, &gui->brain);
    //     xassert(ok);
    // }

    return gui;
}

void pw_destroy_gui(void* _gui)
{
    GUI* gui = _gui;

    sg_set_global(gui->sg);
    sg_shutdown(gui->sg);

    xarr_free(gui->nodes);
    xarr_free(gui->rects);

    int error = FT_Done_Face(gui->ft_face);
    xassert(!error);
    error = FT_Done_FreeType(gui->ft_lib);
    xassert(!error);

    gui->plugin->gui = NULL;
    MY_FREE(gui);
}

bool pw_event(const PWEvent* event)
{
    GUI* gui = event->gui;

    if (!gui || !gui->plugin)
        return false;

    if (event->type == PW_EVENT_RESIZE)
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

    // Logo shader
    {
        float l = -1;
        float r = 1;
        float t = -1;
        float b = 1;

        // clang-format off
        vertex_t verts[] = {
            {l, t, 0,     32767},
            {r, t, 32767, 32767},
            {r, b, 32767, 0},
            {l, b, 0,     0},
        };

        sg_update_buffer(gui->vbo, &SG_RANGE(verts));
        sg_apply_pipeline(gui->pip);

        sg_bindings bind       = {0};
        bind.vertex_buffers[0] = gui->vbo;
        bind.index_buffer      = gui->ibo;
        bind.images[IMG_text_tex]   = gui->img;
        // bind.images[IMG_text_tex]   = gui->brain.img;
        bind.samplers[SMP_text_smp] = gui->smp;

        sg_apply_bindings(&bind);
        sg_draw(0, 6, 1);
    }

    sg_end_pass();
    sg_commit();
    sg_set_global(gui->sg);
}

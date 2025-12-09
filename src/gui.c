#include "libs/kb_text_shape.h"
#include <stdlib.h>
#include <vcruntime.h>
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
#include FT_FREETYPE_H

#include <img.glsl.h>
#include <text.glsl.h>

// #define USE_HARFBUZZ
#ifdef USE_HARFBUZZ
#include <hb.h>

#include <hb-ft.h>
#else
#define KB_TEXT_SHAPE_IMPLEMENTATION
#include "kb_text_shape.h"
#endif // !USE_HARFBUZZ

// TODO
// Bind correct atlas img when drawing text
// Handle variety of text alignments. eg TL, TC, TR, CL, CC, CR, BL, BC, BR
// Handle proper blending of text so glyphs don't clip each other
// Handle proper blending of text so subpixel antialiasing blends with background
// Handle multiple fonts (bold/italic) & font sizes
// Test mixed language strings (Try this? https://github.com/Tehreer/SheenBidi)
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

// https://utf8everywhere.org/
// static const char* MY_TEXT = "abc";
// static const char* MY_TEXT = "Sphinx of black quartz, judge my vow";
// static const char* MY_TEXT = "AV. .W.V.";
// This used to display correctly in my IDE (VSCode) but it appears to be broken. kb_text_shape v1 couldn't properly
// segment the text and struggled to correctly position the hebrew glyphs. v2.0 appears to be perfect! Hoorah
static const char* MY_TEXT = "Приве́т नमस्ते שָׁלוֹם  wow 🐨";
// static const char* MY_TEXT = "UTF8 Приве́т नमस्ते שָׁלוֹם";
// static const char* MY_TEXT = "שָׁלוֹם";
// static const char* MY_TEXT = "UTF8 Приве́т";
// NOTE: in order to correctly shape this text with kbts, you must explicitly say the text is LTR direction
// static const char* MY_TEXT = "-48.37dB + 10";

enum
{
    // MAX_SQUARES  = 1024 * 10 + 512 + 128 + 32 + 8 + 2,
    // MAX_SQUARES = (1 << 16) / 6,
    MAX_SQUARES  = 128,
    MAX_VERTICES = MAX_SQUARES * 4,
    MAX_INDICES  = MAX_SQUARES * 6,

    ATLAS_WIDTH       = 128,
    ATLAS_HEIGHT      = ATLAS_WIDTH,
    ATLAS_ROW_STRIDE  = ATLAS_WIDTH * 4,
    ATLAS_INT16_SHIFT = 8,

    // Sans-serif style typefaces become hard to read below a font size of 7px
    // 8px should be the minimum
    FONT_SIZE        = 12,
    RECTPACK_PADDING = 1,
};
_Static_assert(MAX_INDICES < UINT16_MAX, "UINT16 Overflow");
_Static_assert((ATLAS_WIDTH << ATLAS_INT16_SHIFT) == (1 << 15), "");

// Used to identify a unique glyph.
// TODO: support multiple fonts
union atlas_rect_header
{
    struct
    {
        uint32_t glyphid;
        float    font_size;
    };
    uint64_t data;
};

typedef struct atlas_rect_t
{
    union atlas_rect_header header;

    int pen_offset_x;
    int pen_offset_y;

    uint8_t x, y, w, h;
    sg_view img_view;
} atlas_rect;
_Static_assert(ATLAS_WIDTH <= UINT8_MAX + 1, "");

typedef struct glyph_atlas
{
    sg_view img_view;
    bool    dirty;
    bool    full;
} glyph_atlas;

glyph_atlas glyph_atlas_new()
{
    sg_image img = sg_make_image(&(sg_image_desc){
        .width                = ATLAS_WIDTH,
        .height               = ATLAS_HEIGHT,
        .pixel_format         = SG_PIXELFORMAT_RGBA8,
        .usage.dynamic_update = true,
    });
    xassert(img.id);
    glyph_atlas atlas = {.img_view = sg_make_view(&(sg_view_desc){.texture.image = img})};
    xassert(atlas.img_view.id);
    return atlas;
}

typedef struct GUI
{
    Plugin*      plugin;
    void*        pw;
    _sg_state_t* sg;

    glyph_atlas* glyph_atlases;
    atlas_rect*  rects;

    struct
    {
        int            idx;
        stbrp_context  ctx;
        stbrp_node*    nodes;
        unsigned char* img_data;
    } current_atlas;

    FT_Library ft_lib;
    FT_Face    ft_face;

#ifndef USE_HARFBUZZ
    // kbts_font           kb_font;
    kbts_shape_context* kb_context;

    kbts_glyph* Glyphs;
    uint32_t    GlyphCap;

    int*   Codepoints;
    size_t CodepointCap;
#endif

    // Text pipeline
    sg_pipeline pip;
    sg_buffer   vbo;
    sg_buffer   ibo;
    sg_sampler  smp;

    sg_pipeline img_pip;
    sg_image    img_girl_jacket;
    sg_sampler  sampler_linear;
    sg_sampler  sampler_nearest;

    // struct load_img_t brain;
    size_t   num_vertices;
    size_t   num_indices;
    vertex_t vertices[MAX_VERTICES];
    uint16_t indices[MAX_INDICES];
} GUI;

int raster_glyph(GUI* gui, uint32_t glyph_index)
{
    int num_packed = 0;

    xassert(gui->current_atlas.idx < xarr_len(gui->glyph_atlases));
    glyph_atlas* atlas = gui->glyph_atlases + gui->current_atlas.idx;

    int err = FT_Load_Glyph(gui->ft_face, glyph_index, FT_LOAD_DEFAULT);
    xassert(!err);

    FT_GlyphSlot glyph = gui->ft_face->glyph;

    FT_Render_Mode render_mode = FT_RENDER_MODE_LCD; // subpixel antialiasing, horizontal screen
    FT_Render_Glyph(glyph, render_mode);

    const FT_Bitmap* bmp = &glyph->bitmap;
    xassert(bmp->pixel_mode == FT_PIXEL_MODE_LCD);
    xassert((bmp->width % 3) == 0); // note: FT width is measured in bytes (subpixels)

    // Note all glyphs have height/rows... (spaces?)
    if (bmp->width && bmp->rows)
    {
        int        width_pixels = bmp->width / 3;
        stbrp_rect rect         = {.w = width_pixels + RECTPACK_PADDING, .h = bmp->rows + RECTPACK_PADDING};
        num_packed              = stbrp_pack_rects(&gui->current_atlas.ctx, &rect, 1);

        if (num_packed == 0) // atlas is full
        {
            atlas->full = true;

            sg_view_desc view_desc = sg_query_view_desc(atlas->img_view);
            sg_update_image(
                view_desc.texture.image,
                &(sg_image_data){.mip_levels[0] = {gui->current_atlas.img_data, ATLAS_HEIGHT * ATLAS_ROW_STRIDE}});
            atlas->dirty = false;

            // Clear rectpack
            memset(&gui->current_atlas.ctx, 0, sizeof(gui->current_atlas.ctx));
            stbrp_init_target(
                &gui->current_atlas.ctx,
                ATLAS_WIDTH,
                ATLAS_HEIGHT,
                gui->current_atlas.nodes,
                xarr_len(gui->current_atlas.nodes));

            rect       = (stbrp_rect){.w = width_pixels + RECTPACK_PADDING, .h = bmp->rows + RECTPACK_PADDING};
            num_packed = stbrp_pack_rects(&gui->current_atlas.ctx, &rect, 1);
            xassert(num_packed == 1);

            // make new atlas
            glyph_atlas new_atlas = glyph_atlas_new();
            xarr_push(gui->glyph_atlases, new_atlas);
            gui->current_atlas.idx++;

            atlas = gui->glyph_atlases + gui->current_atlas.idx;
        }

        if (num_packed)
        {
            atlas_rect arect;
            arect.header.glyphid   = glyph_index;
            arect.header.font_size = FONT_SIZE;
            arect.pen_offset_x     = glyph->bitmap_left;
            arect.pen_offset_y     = glyph->bitmap_top;
            arect.x                = rect.x;
            arect.y                = rect.y;
            arect.w                = width_pixels;
            arect.h                = bmp->rows;
            arect.img_view         = atlas->img_view;
            xassert(arect.x + arect.w <= ATLAS_WIDTH);
            xassert(arect.y + arect.h <= ATLAS_HEIGHT);

            xarr_push(gui->rects, arect);

            // println("Printing character \"%c\" to atlas at %dx%d", (char)codepoint, rect.x, rect.y);

            for (int y = 0; y < bmp->rows; y++)
            {
                unsigned char* dst = gui->current_atlas.img_data + (rect.y + y) * ATLAS_ROW_STRIDE + rect.x * 4;
                unsigned char* src = bmp->buffer + y * bmp->pitch;

                for (int x = 0; x < width_pixels; x++, dst += 4, src += 3)
                {
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = 0xff;
                }
            }

            atlas->dirty = true;
        }
    }

    return num_packed;
}

// Get cached rect. Rasters the rect to an atlas if not already cached
atlas_rect* get_glyph_rect(GUI* gui, uint32_t glyph_index)
{
    atlas_rect* rect      = NULL;
    const int   num_rects = xarr_len(gui->rects);
    for (int j = 0; j < num_rects; j++)
    {
        if (gui->rects[j].header.glyphid == glyph_index)
        {
            rect = gui->rects + j;
            return rect;
        }
    }
    int did_raster = raster_glyph(gui, glyph_index);
    if (did_raster)
    {
        xassert(num_rects + 1 == xarr_len(gui->rects));
        rect = gui->rects + num_rects;
    }
    return rect;
}

void draw_glyph(GUI* gui, unsigned glyph_idx, int pen_x, int pen_y)
{
    atlas_rect* rect = get_glyph_rect(gui, glyph_idx);

    if (rect)
    {
        int16_t tex_l = rect->x;
        int16_t tex_t = rect->y;
        int16_t tex_r = rect->x + rect->w;
        int16_t tex_b = rect->y + rect->h;

        xassert(tex_l >= 0 && tex_l < 128);
        xassert(tex_t >= 0 && tex_t < 128);
        xassert(tex_r >= 0 && tex_r < 128);
        xassert(tex_b >= 0 && tex_b < 128);

        // atlas coordinates to INT16 normalised texture coordinates
        tex_l <<= ATLAS_INT16_SHIFT;
        tex_t <<= ATLAS_INT16_SHIFT;
        tex_r <<= ATLAS_INT16_SHIFT;
        tex_b <<= ATLAS_INT16_SHIFT;

        int glyph_left   = pen_x + rect->pen_offset_x;
        int glyph_top    = pen_y - rect->pen_offset_y;
        int glyph_right  = glyph_left + (int)rect->w;
        int glyph_bottom = glyph_top + (int)rect->h;

        const int gui_width  = gui->plugin->width;
        const int gui_height = gui->plugin->height;

        float l = xm_mapf(glyph_left, 0, gui_width, -1, 1);
        float r = xm_mapf(glyph_right, 0, gui_width, -1, 1);
        float t = xm_mapf(glyph_top, 0, gui_height, 1, -1);
        float b = xm_mapf(glyph_bottom, 0, gui_height, 1, -1);

        xassert(gui->num_vertices + 4 <= MAX_VERTICES);
        xassert(gui->num_indices + 6 <= MAX_INDICES);
        uint16_t nvert           = gui->num_vertices;
        uint16_t nidx            = gui->num_indices;
        gui->vertices[nvert + 0] = (vertex_t){l, t, tex_l, tex_t};
        gui->vertices[nvert + 1] = (vertex_t){r, t, tex_r, tex_t};
        gui->vertices[nvert + 2] = (vertex_t){r, b, tex_r, tex_b};
        gui->vertices[nvert + 3] = (vertex_t){l, b, tex_l, tex_b};

        gui->indices[nidx + 0] = nvert + 0;
        gui->indices[nidx + 1] = nvert + 1;
        gui->indices[nidx + 2] = nvert + 2;
        gui->indices[nidx + 3] = nvert + 0;
        gui->indices[nidx + 4] = nvert + 2;
        gui->indices[nidx + 5] = nvert + 3;

        gui->num_vertices += 4;
        gui->num_indices  += 6;
    }
}

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

    // img shader
    {
        gui->vbo = sg_make_buffer(&(sg_buffer_desc){
            .usage.vertex_buffer = true,
            .usage.stream_update = true,
            .size                = sizeof(gui->vertices),
            .label               = "img-vertices"});

        gui->ibo = sg_make_buffer(&(sg_buffer_desc){
            .usage.index_buffer  = true,
            .usage.stream_update = true,
            .size                = sizeof(gui->indices),
            .label               = "img-indices"});

        // sg_update_buffer(gui->ibo, &SG_RANGE(indices));

        sg_shader shd = sg_make_shader(text_shader_desc(sg_query_backend()));
        gui->pip      = sg_make_pipeline(&(sg_pipeline_desc){
                 .shader     = shd,
                 .index_type = SG_INDEXTYPE_UINT16,
                 .layout =
                {.attrs =
                          {[ATTR_text_position].format  = SG_VERTEXFORMAT_FLOAT2,
                           [ATTR_text_texcoord0].format = SG_VERTEXFORMAT_SHORT2N}},
                 .colors[0] =
                {.write_mask = SG_COLORMASK_RGB,
                      .blend =
                          {
                              .enabled        = true,
                              .src_factor_rgb = SG_BLENDFACTOR_ONE,
                              .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR,
                     }},
                 .label = "img-pipeline"});
    }

    int err = FT_Init_FreeType(&gui->ft_lib);
    xassert(!err);
    // const char* font_path = "C:\\Windows\\Fonts\\arialbd.ttf";
    const char* font_path = "C:\\Windows\\Fonts\\seguisb.ttf";
    // const char* font_path = "C:\\Windows\\Fonts\\segoeui.ttf";
    // const char* font_path = "C:\\Windows\\Fonts\\segoeuib.ttf"; // bold
    // const char* font_path = "C:\\Windows\\Fonts\\seguisb.ttf"; // semibold
    // const char* font_path = SRC_DIR XFILES_DIR_STR "assets" XFILES_DIR_STR "EBGaramond-Regular.ttf";
    // const char* font_path = SRC_DIR XFILES_DIR_STR "assets" XFILES_DIR_STR "NotoSansHebrew-Regular.ttf";
    xassert(xfiles_exists(font_path));
    err = FT_New_Face(gui->ft_lib, font_path, 0, &gui->ft_face);
    xassert(!err);
    println("Found %ld glyphs", gui->ft_face->num_glyphs);

    const float DPI = 96;
    FT_Set_Char_Size(gui->ft_face, 0, FONT_SIZE * 64, DPI, DPI);

    gui->current_atlas.idx = 0;
    xarr_setcap(gui->glyph_atlases, 16);
    xarr_setlen(gui->glyph_atlases, 1);
    gui->glyph_atlases[0] = glyph_atlas_new();

    xarr_setlen(gui->current_atlas.nodes, (ATLAS_WIDTH * 2));
    gui->current_atlas.img_data = xcalloc(1, ATLAS_HEIGHT * ATLAS_ROW_STRIDE);
    stbrp_init_target(
        &gui->current_atlas.ctx,
        ATLAS_WIDTH,
        ATLAS_HEIGHT,
        gui->current_atlas.nodes,
        xarr_len(gui->current_atlas.nodes));

    // Pre-render standard latin
    // from "!" to "~" https://www.ascii-code.com/
    for (int codepoint = 33; codepoint < 127; codepoint++)
    {
        FT_UInt glyph_index = FT_Get_Char_Index(gui->ft_face, codepoint);
        raster_glyph(gui, glyph_index);
    }

#ifndef USE_HARFBUZZ
    // Open a font file
    // gui->kb_font  = kbts_FontFromFile(font_path);
    gui->kb_context = kbts_CreateShapeContext(0, 0);
    kbts_ShapePushFontFromFile(gui->kb_context, font_path, 0);
#endif

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
    const char*       img_path = SRC_DIR XFILES_DIR_STR "assets" XFILES_DIR_STR "img_girl.jpg";
    struct load_img_t img_girl;
    bool              ok = load_image(img_path, &img_girl);
    if (ok)
        gui->img_girl_jacket = img_girl.img;

    return gui;
}

void pw_destroy_gui(void* _gui)
{
    GUI* gui = _gui;

    sg_set_global(gui->sg);
    sg_shutdown(gui->sg);

    xfree(gui->current_atlas.img_data);
    xarr_free(gui->current_atlas.nodes);
    xarr_free(gui->rects);
    xarr_free(gui->glyph_atlases);

    int error = FT_Done_Face(gui->ft_face);
    xassert(!error);
    error = FT_Done_FreeType(gui->ft_lib);
    xassert(!error);

#ifndef USE_HARFBUZZ
    // kbts_FreeFont(&gui->kb_font);
    kbts_DestroyShapeContext(gui->kb_context);
    xfree(gui->Codepoints);
    xfree(gui->Glyphs);
#endif

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

    gui->num_indices  = 0;
    gui->num_vertices = 0;

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

    // sg_apply_pipeline(gui->img_pip);
    // sg_apply_bindings(&(sg_bindings){
    //     .images[0]   = gui->img_girl_jacket,
    //     .samplers[0] = gui->sampler_nearest,
    // });
    // sg_draw(0, 3, 1);

    size_t my_text_len = strlen(MY_TEXT);

    int max_font_height_pixels = (gui->ft_face->size->metrics.ascender - gui->ft_face->size->metrics.descender) >> 6;

    int pen_x = 0;
    int pen_y = max_font_height_pixels + (gui->ft_face->size->metrics.descender >> 6);

    // Move pen to centre of gui
    // pen_x += 10;             // padding
    pen_x  = gui_width / 2;  // padding
    pen_y += gui_height / 2; // Vertical centre
    pen_y -= FONT_SIZE / 2;

// Harfbuzz
#if defined(USE_HARFBUZZ)
    hb_buffer_t* buf = hb_buffer_create();
    // hb_language_t default_language = hb_language_get_default();
    // xassert(default_language != NULL);
    // hb_buffer_set_language(buf, default_language);

    hb_buffer_add_utf8(buf, MY_TEXT, my_text_len, 0, my_text_len);
    // If we know the language:
    // hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
    // hb_buffer_set_script(buf, HB_SCRIPT_LATIN);
    // hb_buffer_set_language(buf, hb_language_from_string("en", -1));
    // If we don't know the language:
    hb_buffer_guess_segment_properties(buf);

    hb_font_t* font = hb_ft_font_create(gui->ft_face, NULL);
    hb_shape(font, buf, NULL, 0);

    unsigned int         glyph_count;
    hb_glyph_info_t*     glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);
    hb_glyph_position_t* glyph_pos  = hb_buffer_get_glyph_positions(buf, &glyph_count);

    for (unsigned int i = 0; i < glyph_count; i++)
    {
        const hb_glyph_info_t*     info = glyph_info + i;
        const hb_glyph_position_t* pos  = glyph_pos + i;

        draw_glyph(gui, info->codepoint, pen_x, pen_y);

        pen_x += pos->x_advance >> 6;
        pen_y += pos->y_advance >> 6;

        // break;
    }

    hb_font_destroy(font);
    hb_buffer_destroy(buf);
#endif // USE_HARFBUZZ

#if !defined(USE_HARFBUZZ)
    kbts_ShapeBegin(gui->kb_context, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);
    kbts_ShapeUtf8(gui->kb_context, MY_TEXT, my_text_len, KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
    kbts_ShapeEnd(gui->kb_context);

    const FT_Size_Metrics* FtSizeMetrics = &gui->ft_face->size->metrics;

    // Layout runs naively left to right.
    kbts_run Run;
    int      CursorX = 0, CursorY = 0;
    while (kbts_ShapeRun(gui->kb_context, &Run))
    {
        kbts_glyph* Glyph;
        while (kbts_GlyphIteratorNext(&Run.Glyphs, &Glyph))
        {
            int GlyphX = CursorX + Glyph->OffsetX;
            int GlyphY = CursorY + Glyph->OffsetY;

            int glyph_x = ((GlyphX >> 6) * FtSizeMetrics->x_scale) >> 16;
            int glyph_y = ((GlyphY >> 6) * FtSizeMetrics->y_scale) >> 16;
            draw_glyph(gui, Glyph->Id, pen_x + glyph_x, pen_y + glyph_y);

            CursorX += Glyph->AdvanceX;
            CursorY += Glyph->AdvanceY;
        }
    }

#endif // !USE_HARFBUZZ

    // IMG shader
    if (gui->num_indices)
    {
        glyph_atlas* atlas = gui->glyph_atlases + gui->current_atlas.idx;
        if (atlas->dirty)
        {
            sg_view_desc view_desc = sg_query_view_desc(atlas->img_view);
            sg_update_image(
                view_desc.texture.image,
                &(sg_image_data){
                    .mip_levels[0] = {
                        .ptr  = gui->current_atlas.img_data,
                        .size = ATLAS_HEIGHT * ATLAS_ROW_STRIDE,
                    }});
            atlas->dirty = false;
        }

        sg_range vert_range = {.ptr = gui->vertices, .size = sizeof(gui->vertices[0]) * gui->num_vertices};
        sg_range idx_range  = {.ptr = gui->indices, .size = sizeof(gui->indices[0]) * gui->num_indices};
        sg_update_buffer(gui->vbo, &vert_range);
        sg_update_buffer(gui->ibo, &idx_range);

        sg_apply_pipeline(gui->pip);

        sg_bindings bind            = {0};
        bind.vertex_buffers[0]      = gui->vbo;
        bind.index_buffer           = gui->ibo;
        bind.views[VIEW_text_tex]   = atlas->img_view;
        bind.samplers[SMP_text_smp] = gui->sampler_nearest;

        sg_apply_bindings(&bind);
        sg_draw(0, gui->num_indices, 1);
    }

    sg_end_pass();
    sg_commit();
    sg_set_global(NULL);
}

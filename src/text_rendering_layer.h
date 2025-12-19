#ifndef TEXT_H
#define TEXT_H
#include <sokol_gfx.h>

typedef struct TextLayer TextLayer;

TextLayer* text_layer_new(const char* font_path);
void       text_layer_destroy(TextLayer* gui);

void text_layer_prerender_ascii(TextLayer* gui, float font_size);
void text_layer_draw_text(TextLayer* gui, const char* text_start, const char* text_end, int x, int y, float font_size);

// Handle all the buffer uploads etc
void text_layer_draw(TextLayer* gui, sg_sampler sampler, int gui_width, int gui_height);

#endif // TEXT_H

#ifdef TEXT_IMPL
#undef TEXT_IMPL

#include "common.h"

#include <kb_text_shape.h>
#include <stb_rect_pack.h>
#include <text.glsl.h>
#include <xhl/alloc.h>
#include <xhl/array.h>
// #include <xhl/array2.h>
#include <xhl/debug.h>
#include <xhl/files.h>

#if !defined(RASTER_STB_TRUETYPE) && !defined(RASTER_FREETYPE_SINGLECHANNEL) && !defined(RASTER_FREETYPE_MULTICHANNEL)
// #define RASTER_STB_TRUETYPE
#define RASTER_FREETYPE_MULTICHANNEL
// #define RASTER_FREETYPE_SINGLECHANNEL
#endif
#if defined(RASTER_FREETYPE_SINGLECHANNEL) || defined(RASTER_FREETYPE_MULTICHANNEL)
#define RASTER_FREETYPE
#endif

#if defined(RASTER_FREETYPE_SINGLECHANNEL) || defined(RASTER_FREETYPE_MULTICHANNEL)
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

#if defined(RASTER_STB_TRUETYPE)
#include <stb_truetype.h>
#endif

#ifndef MAX_GLYPHS
#define MAX_GLYPHS 128
#endif

enum
{
#if defined(RASTER_FREETYPE_SINGLECHANNEL) || defined(RASTER_FREETYPE_MULTICHANNEL)
#if defined(RASTER_FREETYPE_SINGLECHANNEL)
    PLATFORM_FT_RENDER_MODE   = FT_RENDER_MODE_NORMAL,
    PLATFORM_FT_PIXEL_MODE    = FT_PIXEL_MODE_GRAY,
    PLATFORM_FT_BITMAP_WIDTH  = 1,
    PLATFORM_TEXTURE_CHANNELS = 1,
    PLATFORM_SG_PIXEL_FORMAT  = SG_PIXELFORMAT_R8,
#else
    PLATFORM_FT_RENDER_MODE   = FT_RENDER_MODE_LCD, // subpixel antialiasing, horizontal screen
    PLATFORM_FT_PIXEL_MODE    = FT_PIXEL_MODE_LCD,
    PLATFORM_FT_BITMAP_WIDTH  = 3,
    PLATFORM_TEXTURE_CHANNELS = 4,
    PLATFORM_SG_PIXEL_FORMAT  = SG_PIXELFORMAT_RGBA8,
#endif
#endif // RASTER_FREETYPE_SINGLECHANNEL || RASTER_FREETYPE_MULTICHANNEL

#ifdef RASTER_STB_TRUETYPE
    PLATFORM_TEXTURE_CHANNELS = 1,
    PLATFORM_SG_PIXEL_FORMAT  = SG_PIXELFORMAT_R8,
#endif

// TODO: make this state and not an enum
#if defined(__APPLE__)
    PLATFORM_BACKING_SCALE_FACTOR = 2,
#else
    PLATFORM_BACKING_SCALE_FACTOR = 1,
#endif

    ATLAS_SIZE_SHIFT   = 8,
    ATLAS_WIDTH        = (1 << ATLAS_SIZE_SHIFT),
    ATLAS_HEIGHT       = ATLAS_WIDTH,
    ATLAS_ROW_STRIDE   = ATLAS_WIDTH * PLATFORM_TEXTURE_CHANNELS,
    ATLAS_UINT16_SHIFT = (16 - ATLAS_SIZE_SHIFT),

    RECTPACK_PADDING = 1,
};
_Static_assert((ATLAS_WIDTH << ATLAS_UINT16_SHIFT) == (1 << 16), "");

typedef struct
{
    float   x, y;
    int16_t u, v;
} vertex_t;

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

    int16_t x, y, w, h;

    int16_t pen_offset_x;
    int16_t pen_offset_y;

    sg_view img_view;
} atlas_rect;
_Static_assert(ATLAS_WIDTH <= (1llu << 16), "");

typedef struct glyph_atlas
{
    sg_view img_view;
    bool    dirty;
    bool    full;
} glyph_atlas;

struct TextLayer
{

    glyph_atlas* glyph_atlases;
    atlas_rect*  rects;

    struct
    {
        int            idx;
        stbrp_context  ctx;
        stbrp_node*    nodes;
        unsigned char* img_data;
    } current_atlas;

    void*  fontdata;
    size_t fontdata_size;

#ifdef RASTER_FREETYPE
    FT_Library ft_lib;
    FT_Face    ft_face;
#endif

#ifdef RASTER_STB_TRUETYPE
    stbtt_fontinfo fontinfo;
#endif

    kbts_shape_context* kb_context;

    // Text pipeline
    sg_pipeline text_pip;
    sg_buffer   text_sbo;
    sg_view     text_sbv;
    sg_sampler  text_smp;

    size_t        text_buffer_len;
    text_buffer_t text_buffer[MAX_GLYPHS];
};

glyph_atlas glyph_atlas_new()
{
    sg_image img = sg_make_image(&(sg_image_desc){
        .width                = ATLAS_WIDTH,
        .height               = ATLAS_HEIGHT,
        .pixel_format         = PLATFORM_SG_PIXEL_FORMAT,
        .usage.dynamic_update = true,
    });
    xassert(img.id);
    glyph_atlas atlas = {.img_view = sg_make_view(&(sg_view_desc){.texture.image = img})};
    xassert(atlas.img_view.id);
    return atlas;
}

#ifdef RASTER_FREETYPE
int raster_glyph(TextLayer* gui, uint32_t glyph_index, float font_size)
{
    int num_packed = 0;

    xassert(gui->current_atlas.idx < xarr_len(gui->glyph_atlases));
    glyph_atlas* atlas = gui->glyph_atlases + gui->current_atlas.idx;

    const float DPI = 96;
    FT_Set_Char_Size(gui->ft_face, 0, font_size * 64 * PLATFORM_BACKING_SCALE_FACTOR, DPI, DPI);

    int err = FT_Load_Glyph(gui->ft_face, glyph_index, FT_LOAD_DEFAULT);
    xassert(!err);

    FT_GlyphSlot glyph = gui->ft_face->glyph;

    FT_Render_Mode render_mode = PLATFORM_FT_RENDER_MODE;
    FT_Render_Glyph(glyph, render_mode);

    const FT_Bitmap* bmp = &glyph->bitmap;
    xassert(bmp->pixel_mode == PLATFORM_FT_PIXEL_MODE);
    xassert((bmp->width % PLATFORM_FT_BITMAP_WIDTH) == 0); // note: FT width is measured in bytes (subpixels)

    // Note all glyphs have height/rows... (spaces?)
    if (bmp->width && bmp->rows)
    {
        int        width_pixels = bmp->width / PLATFORM_FT_BITMAP_WIDTH;
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
                ATLAS_WIDTH - RECTPACK_PADDING,
                ATLAS_HEIGHT - RECTPACK_PADDING,
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
            arect.header.font_size = font_size;
            arect.pen_offset_x     = glyph->bitmap_left / PLATFORM_BACKING_SCALE_FACTOR;
            arect.pen_offset_y     = glyph->bitmap_top / PLATFORM_BACKING_SCALE_FACTOR;
            arect.x                = rect.x + RECTPACK_PADDING;
            arect.y                = rect.y + RECTPACK_PADDING;
            arect.w                = width_pixels;
            arect.h                = bmp->rows;
            arect.img_view         = atlas->img_view;
            xassert(arect.x + arect.w <= ATLAS_WIDTH);
            xassert(arect.y + arect.h <= ATLAS_HEIGHT);

            xarr_push(gui->rects, arect);

            // println("Printing character \"%c\" to atlas at %dx%d", (char)codepoint, rect.x, rect.y);

            for (int y = 0; y < bmp->rows; y++)
            {
#if defined(RASTER_FREETYPE_SINGLECHANNEL)
                unsigned char* dst = gui->current_atlas.img_data + (arect.y + y) * ATLAS_ROW_STRIDE + arect.x;
                unsigned char* src = bmp->buffer + y * bmp->pitch;

                unsigned char(*src_view)[512]  = (void*)src;
                src_view                      += 0;
                unsigned char(*dst_view)[512]  = (void*)dst;
                dst_view                      += 0;

                memcpy(dst, src, width_pixels);

                dst_view += 0;
#else
                unsigned char* dst = gui->current_atlas.img_data + (arect.y + y) * ATLAS_ROW_STRIDE + arect.x * PLATFORM_TEXTURE_CHANNELS;
                unsigned char* src = bmp->buffer + y * bmp->pitch;

                for (int x = 0; x < width_pixels; x++, dst += PLATFORM_TEXTURE_CHANNELS, src += PLATFORM_FT_BITMAP_WIDTH)
                {
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = 0;
                }
#endif
            }

            atlas->dirty = true;
        }
    }

    return num_packed;
}
#endif // RASTER_FREETYPE
#ifdef RASTER_STB_TRUETYPE
int raster_glyph(TextLayer* gui, uint32_t glyph_index, float font_size)
{
    int num_packed = 0;

    xassert(gui->current_atlas.idx < xarr_len(gui->glyph_atlases));
    glyph_atlas* atlas = gui->glyph_atlases + gui->current_atlas.idx;

    int advanceWidth = 0, leftSideBearing = 0;
    int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
    // TODO: figure out what I should be using here...
    // TODO: figure out how to get rasterizer to match the same height as the text shaper
    float scale_pixel_height = stbtt_ScaleForPixelHeight(&gui->fontinfo, font_size * PLATFORM_BACKING_SCALE_FACTOR * 2);
    // float scale_emtopixels = stbtt_ScaleForMappingEmToPixels(&gui->fontinfo, font_size *
    // PLATFORM_BACKING_SCALE_FACTOR);
    float scale = scale_pixel_height;
    stbtt_GetGlyphHMetrics(&gui->fontinfo, glyph_index, &advanceWidth, &leftSideBearing);
    stbtt_GetGlyphBitmapBox(&gui->fontinfo, glyph_index, scale, scale, &ix0, &iy0, &ix1, &iy1);

    int iw = ix1 - ix0;
    int ih = iy1 - iy0;

    if (iw && ih)
    {
        stbrp_rect rect = {.w = iw + RECTPACK_PADDING, .h = ih + RECTPACK_PADDING};
        num_packed      = stbrp_pack_rects(&gui->current_atlas.ctx, &rect, 1);

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

            rect       = (stbrp_rect){.w = iw + RECTPACK_PADDING, .h = ih + RECTPACK_PADDING};
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
            arect.header.font_size = font_size;
            arect.pen_offset_x     = ix0 / PLATFORM_BACKING_SCALE_FACTOR;
            arect.pen_offset_y     = -iy0 / PLATFORM_BACKING_SCALE_FACTOR;
            arect.x                = rect.x;
            arect.y                = rect.y;
            arect.w                = iw;
            arect.h                = ih;
            arect.img_view         = atlas->img_view;
            xassert(arect.x + arect.w <= ATLAS_WIDTH);
            xassert(arect.y + arect.h <= ATLAS_HEIGHT);

            unsigned char* dst =
                gui->current_atlas.img_data + rect.y * ATLAS_ROW_STRIDE + rect.x * PLATFORM_TEXTURE_CHANNELS;

            stbtt_MakeGlyphBitmap(&gui->fontinfo, dst, iw, ih, ATLAS_ROW_STRIDE, scale, scale, glyph_index);

            xarr_push(gui->rects, arect);

            atlas->dirty = true;
        }
    }

    // stbtt_MakeGlyphBitmap(&gui->fontinfo, output, outWidth, outHeight, outStride, scaleX, scaleY, glyph_index);

    return num_packed;
}
#endif

// Get cached rect. Rasters the rect to an atlas if not already cached
// TODO: also compare font id
// TODO: use fallback fonts. This may require accepting utf32 codepoints to detect language
const atlas_rect* get_glyph_rect(TextLayer* gui, uint32_t glyph_index, float font_size)
{
    const int num_rects = xarr_len(gui->rects);

    const union atlas_rect_header header = {.glyphid = glyph_index, .font_size = font_size};

    for (int j = 0; j < num_rects; j++)
    {
        if (gui->rects[j].header.data == header.data)
            return gui->rects + j;
    }

    int did_raster = raster_glyph(gui, glyph_index, font_size);
    if (did_raster)
    {
        xassert(num_rects + 1 == xarr_len(gui->rects));
        return gui->rects + num_rects;
    }

    // Note: this stub has a texture view id of 0
    // sokol_gfx should assert in debug mode when trying to bind a texture view with an id of 0
    // In release it should skip all draws using that view. This is our desired behaviour
    static const atlas_rect stub_rect = {0};
    return &stub_rect;
}

void draw_glyph(TextLayer* gui, int pen_x, int pen_y, unsigned glyph_idx, float font_size)
{
    const atlas_rect* rect = get_glyph_rect(gui, glyph_idx, font_size);

    if (gui->text_buffer_len < ARRLEN(gui->text_buffer))
    {
        uint32_t tex_l = rect->x;
        uint32_t tex_t = rect->y;
        uint32_t tex_r = rect->x + rect->w;
        uint32_t tex_b = rect->y + rect->h;

        xassert(tex_l >= 0 && tex_l < ATLAS_WIDTH);
        xassert(tex_t >= 0 && tex_t < ATLAS_HEIGHT);
        xassert(tex_r >= 0 && tex_r < ATLAS_WIDTH);
        xassert(tex_b >= 0 && tex_b < ATLAS_HEIGHT);

        // atlas coordinates to INT16 normalised texture coordinates
        tex_l <<= ATLAS_UINT16_SHIFT;
        tex_t <<= ATLAS_UINT16_SHIFT;
        tex_r <<= ATLAS_UINT16_SHIFT;
        tex_b <<= ATLAS_UINT16_SHIFT;
        xassert(tex_l < (1 << 16));
        xassert(tex_t < (1 << 16));
        xassert(tex_r < (1 << 16));
        xassert(tex_b < (1 << 16));

        int glyph_left   = pen_x + (int)rect->pen_offset_x;
        int glyph_top    = pen_y - (int)rect->pen_offset_y;
        int glyph_right  = glyph_left + (int)rect->w / PLATFORM_BACKING_SCALE_FACTOR;
        int glyph_bottom = glyph_top + (int)rect->h / PLATFORM_BACKING_SCALE_FACTOR;

        xassert(glyph_left < (1 << 16));
        xassert(glyph_top < (1 << 16));
        xassert(glyph_right < (1 << 16));
        xassert(glyph_bottom < (1 << 16));

        text_buffer_t* obj        = gui->text_buffer + gui->text_buffer_len;
        obj->coord_topleft[0]     = glyph_left;
        obj->coord_topleft[1]     = glyph_top;
        obj->coord_bottomright[0] = glyph_right;
        obj->coord_bottomright[1] = glyph_bottom;
        obj->tex_topleft          = tex_l | (tex_t << 16);
        obj->tex_bottomright      = tex_r | (tex_b << 16);
        // obj->tex_topleft     = tex_t | (tex_l << 16);
        // obj->tex_bottomright = tex_b | (tex_r << 16);

        gui->text_buffer_len++;
    }
}

TextLayer* text_layer_new(const char* font_path)
{
    TextLayer* gui = xcalloc(1, sizeof(*gui));

    xarr_setcap(gui->rects, 64);
    gui->text_sbo = sg_make_buffer(&(sg_buffer_desc){
        .usage.storage_buffer = true,
        .usage.stream_update  = true,
        .size                 = sizeof(gui->text_buffer),
        .label                = "text SBO",
    });
    xassert(gui->text_sbo.id);
    gui->text_sbv = sg_make_view(&(sg_view_desc){
        .storage_buffer = gui->text_sbo,
    });
    xassert(gui->text_sbv.id);

#if defined(RASTER_FREETYPE_MULTICHANNEL)
    sg_shader shd = sg_make_shader(text_multichannel_shader_desc(sg_query_backend()));
#else
    sg_shader shd = sg_make_shader(text_singlechannel_shader_desc(sg_query_backend()));
#endif

    sg_pipeline_desc pip_desc = {.shader = shd, .label = "img-pipeline"};

#if defined(RASTER_FREETYPE_MULTICHANNEL)
    pip_desc.colors[0] = (sg_color_target_state){
        .write_mask = SG_COLORMASK_RGB,
        .blend      = {
                 .enabled        = true,
                 .src_factor_rgb = SG_BLENDFACTOR_ONE,
                 .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR,
        }};
#else
    pip_desc.colors[0] = (sg_color_target_state){
        .write_mask = SG_COLORMASK_RGBA,
        .blend      = {
                 .enabled          = true,
                 .src_factor_rgb   = SG_BLENDFACTOR_SRC_ALPHA,
                 .src_factor_alpha = SG_BLENDFACTOR_ONE,
                 .dst_factor_rgb   = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                 .dst_factor_alpha = SG_BLENDFACTOR_ONE,
        }};

#endif

    gui->text_pip      = sg_make_pipeline(&pip_desc);
    bool did_read_file = xfiles_read(font_path, &gui->fontdata, &gui->fontdata_size);
    xassert(did_read_file);
    if (did_read_file)
    {
#ifdef RASTER_FREETYPE
        int err = FT_Init_FreeType(&gui->ft_lib);
        xassert(!err);
        err = FT_New_Memory_Face(gui->ft_lib, gui->fontdata, gui->fontdata_size, 0, &gui->ft_face);
        xassert(!err);
#endif // RASTER_FREETYPE
#ifdef RASTER_STB_TRUETYPE
        int offset = stbtt_GetFontOffsetForIndex(gui->fontdata, 0);
        xassert(offset != -1);
        if (offset != -1)
        {
            int ok = stbtt_InitFont(&gui->fontinfo, gui->fontdata, offset);
            xassert(ok != 0);
        }
#endif

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

        // Open a font file
        gui->kb_context = kbts_CreateShapeContext(0, 0);
        kbts_ShapePushFontFromMemory(gui->kb_context, gui->fontdata, gui->fontdata_size, 0);
    }

    return gui;
}

void text_layer_destroy(TextLayer* gui)
{
    xfree(gui->current_atlas.img_data);
    xarr_free(gui->current_atlas.nodes);
    xarr_free(gui->rects);
    xarr_free(gui->glyph_atlases);

#ifdef RASTER_FREETYPE
    int error = FT_Done_Face(gui->ft_face);
    xassert(!error);
    error = FT_Done_FreeType(gui->ft_lib);
    xassert(!error);
#endif // RASTER_FREETYPE

    kbts_DestroyShapeContext(gui->kb_context);

    XFILES_FREE(gui->fontdata);

    xfree(gui);
}

void text_layer_prerender_ascii(TextLayer* gui, float font_size)
{
    // Pre-render standard latin
    // from "!" to "~" https://www.ascii-code.com/
    for (int codepoint = 33; codepoint < 127; codepoint++)
    {
#if defined(RASTER_FREETYPE)
        FT_UInt glyph_index = FT_Get_Char_Index(gui->ft_face, codepoint);
#elif defined(RASTER_STB_TRUETYPE)
        uint32_t glyph_index = stbtt_FindGlyphIndex(&gui->fontinfo, codepoint);
#endif
        raster_glyph(gui, glyph_index, font_size);
    }
}

void text_layer_draw_text(TextLayer* gui, const char* text_start, const char* text_end, int x, int y, float font_size)
{
    if (text_end == NULL)
        text_end = text_start + strlen(text_start);
    kbts_ShapeBegin(gui->kb_context, KBTS_DIRECTION_DONT_KNOW, KBTS_LANGUAGE_DONT_KNOW);
    kbts_ShapeUtf8(gui->kb_context, text_start, text_end - text_start, KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
    kbts_ShapeEnd(gui->kb_context);

#if defined(RASTER_FREETYPE)
    const FT_Size_Metrics* FtSizeMetrics  = &gui->ft_face->size->metrics;
    int                    x_scale        = FtSizeMetrics->x_scale;
    int                    y_scale        = FtSizeMetrics->y_scale;
    x_scale                              /= PLATFORM_BACKING_SCALE_FACTOR;
    y_scale                              /= PLATFORM_BACKING_SCALE_FACTOR;

    int max_font_height_pixels = (gui->ft_face->size->metrics.ascender - gui->ft_face->size->metrics.descender) >> 6;
    int pen_y_offset           = max_font_height_pixels + (gui->ft_face->size->metrics.descender >> 6);
#endif
#if defined(RASTER_STB_TRUETYPE)
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&gui->fontinfo, &ascent, &descent, &lineGap);

    int max_font_height_pixels = (ascent + descent) >> 6;
    int pen_y_offset           = max_font_height_pixels + (descent >> 6);

    // TODO: figure out hwo to scale with STB_TRUETYPE
    int x_scale = 32768;
    int y_scale = 32768;
#endif

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

            int glyph_x = ((GlyphX >> 6) * x_scale) >> 16;
            int glyph_y = ((GlyphY >> 6) * y_scale) >> 16;
            draw_glyph(gui, x + glyph_x, y + glyph_y + pen_y_offset, Glyph->Id, font_size);

            CursorX += Glyph->AdvanceX;
            CursorY += Glyph->AdvanceY;
        }
    }
}

void text_layer_draw(TextLayer* gui, sg_sampler sampler, int gui_width, int gui_height)
{
    if (gui->text_buffer_len)
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

        sg_range sbo_range = {.ptr = gui->text_buffer, .size = sizeof(gui->text_buffer[0]) * gui->text_buffer_len};
        sg_update_buffer(gui->text_sbo, &sbo_range);

        sg_apply_pipeline(gui->text_pip);

        sg_bindings bind            = {0};
        bind.views[VIEW_sb_text]    = gui->text_sbv;
        bind.views[VIEW_text_tex]   = atlas->img_view;
        bind.samplers[SMP_text_smp] = sampler; // nearest neighbour

        sg_apply_bindings(&bind);

        vs_text_uniforms_t uniforms = {
            .size = {gui_width, gui_height},
        };
        sg_apply_uniforms(UB_vs_text_uniforms, &SG_RANGE(uniforms));
        sg_draw(0, 6 * gui->text_buffer_len, 1);
    }

    gui->text_buffer_len = 0;
}

#endif // TEXT_IMPL

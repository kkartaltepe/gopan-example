#ifndef GP_HEADER_GP_H
#define GP_HEADER_GP_H

#include <stdbool.h>
#include <stdint.h>
#include <cairo/cairo.h>
#include <hb.h>

#include "gp_props.h"
#include "gp_ttf.h"

// Glyph positions are integers so we must scale them for subpixel accuracy.
// When drawing divide by this scale for pixel coordinates.
#define GP_SHAPE_SCALE 2048

typedef struct gp_runes {
	uint32_t *data;
	uint32_t len;
} gp_runes_t;

typedef struct gp_run {
	uint32_t start;
	uint32_t end;

	int16_t level; // embedding level. Even values are LTR, odd values are RTL.
	int32_t font_pri;
	enum gp_script script;
	enum gp_width width;
	struct gp_face_t *font;
	uint32_t ppem; // track ppem in run as this is baked into glyphs but not for bitmaps
	hb_buffer_t *glyphs;
} gp_run_t;

// Helper to convert utf8 to codepoints
void gp_utf8_to_runes(const char *utf8, uint32_t len, uint32_t dst_cap,
                      uint32_t *dst, uint32_t *dst_len);

// analyze returns a set of runs composing all the information to properly
// render the provided text.  runs_out will contain the chosen font for
// rendering and glyphs to render.  fs and fs_color represent a list of fonts
// providing full unicode coverage ordered by preference. fs_color is used for
// emojis, if you prefer text representation use a text eomji fontset for
// fs_color.  lang represents the language to use for unified codepoints (e.g.
// CJK unified characters). getlocale() is reasonable if you do not have more
// information. Pass a font_size in pixels to enable hinting, 0 disables hinting.
bool gp_analyze(gp_runes_t runes, struct gp_face_set_t faces,
                const char *lang, gp_run_t **runs_out, uint32_t *len, uint32_t font_size);

// Free data from gp_run_t
void gp_run_destroy(gp_run_t *runs, uint32_t len);

// Helper to draw onto a cairo surface if you dont want to implement rendering yourself.
void gp_draw_cairo(cairo_t *cr, gp_run_t *runs, uint32_t len);
#endif

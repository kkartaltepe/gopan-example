// We use the HB_EXPERIMENTAL_API to access hb drawing functions.
#include <fribidi.h>
#define HB_EXPERIMENTAL_API
#include <hb.h>
#include <hb-ot.h>
#include <cairo/cairo.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "gp.h"
#include "gp_props.h"
#include "gp_ttf.h"

#define UNUSED(x) (void)(x)

typedef struct gp_run_iter {
	uint32_t start;
	uint32_t at;

	int16_t level;
	enum gp_script script;
	enum gp_width width;
	struct gp_face_t *font;
	uint32_t font_pri; // priority of current font
} gp_run_iter;

bool face_has_rune(struct gp_face_t *face, uint32_t rune)
{
	return hb_set_has(face->coverage, rune);
}

// Out put the highest priority font that has this glyph and its priority.
struct gp_face_t *choose_font_for(uint32_t rune, struct gp_face_set_t faces,
                                  uint32_t *priority)
{
	for (uint32_t j = 0; j < faces.len; j++) {
		if (face_has_rune(&faces.faces[j], rune)) {
			if (priority)
				*priority = (uint32_t)j;
			return &faces.faces[j];
		}
	}
	printf("Failed to find matching font for U+%X in %d\n", rune, faces.len);
	if (priority) // hope you dont have more than 4b fonts.
		*priority = 0xFFFFFFFF;
	return NULL;
}

bool width_eql(enum gp_width l, enum gp_width r)
{
	bool lb = (l == GP_WIDTH_NEUTRAL) || (l == GP_WIDTH_NARROW) ||
	          (l == GP_WIDTH_HALFWIDTH);
	bool rb = (r == GP_WIDTH_NEUTRAL) || (r == GP_WIDTH_NARROW) ||
	          (r == GP_WIDTH_HALFWIDTH);
	return lb == rb;
}

bool is_space(uint32_t rune)
{
	// pango uses unicode type
	// control/format/surrogate/linesep/parasep/spacesep
	// along with 0x1680u (whitespace in ucd 11)
	return (0x0009 <= rune && rune <= 0x000D) || 0x0020 == rune ||
	       0x0085 == rune || 0x00A0 == rune || 0x1680 == rune ||
	       (0x2000 <= rune && rune <= 0x200D) || 0x2028 == rune ||
	       0x2029 == rune || 0x202F == rune || 0x205F == rune || 0x3000 == rune;
}

bool is_variant_sel(uint32_t rune)
{
	return (0xfe00 <= rune && rune <= 0xfe0f) ||
	       (0xe0100 <= rune && rune <= 0xe01ef);
}

void gp_run_destroy(gp_run_t *runs, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		hb_buffer_destroy(runs[i].glyphs);
	}
	free(runs);
}

void gp_itemize(gp_runes_t runes, struct gp_face_set_t faces,
                FriBidiLevel *levels, gp_run_t **runs_out, uint32_t *len)
{
	// TODO: maintain paired chars (paren/quotes/etc)? Prefering higher priority fonts mostly fixed this.

	gp_run_iter iter = {0};
	gp_run_t *runs = malloc(sizeof(gp_run_t) * 256);
	size_t r = 0;

	// spaces are ambiguous skip any leading spaces.
	while (is_space(runes.data[iter.at]) && iter.at < runes.len) {
		iter.at++;
	}
	iter.width = gp_rune_width(runes.data[iter.at]);
	iter.script = gp_rune_script(runes.data[iter.at]);
	iter.level = levels[iter.at];
	iter.font = choose_font_for(runes.data[iter.at], faces, &iter.font_pri);

	for (; iter.at < runes.len; iter.at++) {
		bool changed = false;
		uint32_t rune = runes.data[iter.at];
		// Just dont break runs on whitespace.
		// TODO: handle (emoji) variant selectors (requires another parser)
		if (is_space(rune) || is_variant_sel(rune)) {
			continue;
		}

		if (iter.font == NULL) {
			// we delayed choosing font until non-space so dont mark changed.
			iter.font = choose_font_for(rune, faces, &iter.font_pri);
		}

		enum gp_width width = gp_rune_width(rune);
		changed |=
		        (iter.width == GP_WIDTH_AMBIGUOUS &&
		         !(width == GP_WIDTH_AMBIGUOUS || width == GP_WIDTH_NEUTRAL));

		struct gp_face_t *face = NULL;
		uint32_t font_pri = 0xFFFFFFFF;
		// Dont break for whitespace, this also helps with joiners.
		if (iter.font != NULL) {
			struct gp_face_t *face_test =
			        choose_font_for(rune, faces, &font_pri);
			if (face_test != NULL && ((font_pri < iter.font_pri) ||
			                          !face_has_rune(iter.font, rune))) {
				changed |= true;
				face = face_test;
			}
		}

		enum gp_script script = gp_rune_script(rune);
		changed |= iter.script != script;

		int16_t level = levels[iter.at];
		changed |= iter.level != level;

		// Terminate current run on attribute changes or end of text.
		if (changed) {
			runs[r].start = iter.start;
			runs[r].end = iter.at;
			runs[r].script = iter.script;
			runs[r].width = iter.width;
			runs[r].level = iter.level;
			if (iter.font == NULL) {
				printf("run had no font1???\n");
				iter.font = &faces.faces[0];
			}
			runs[r].font = iter.font;
			runs[r].font_pri = iter.font_pri;

			iter.start = iter.at;
			iter.width = width;
			iter.script = script;
			iter.level = level;
			iter.font = face;
			iter.font_pri = font_pri;
			if (face == NULL && !is_space(rune)) {
				iter.font = choose_font_for(rune, faces, &iter.font_pri);
			}
			r++;
			assert(r < 256);
		}
	}
	runs[r].start = iter.start;
	runs[r].end = iter.at;
	runs[r].script = iter.script;
	runs[r].width = iter.width;
	runs[r].level = iter.level;
	if (iter.font == NULL) {
		printf("run had no font2???\n");
		iter.font = &faces.faces[0];
	}
	runs[r].font = iter.font;
	runs[r].font_pri = iter.font_pri;

	*runs_out = runs;
	*len = r + 1;
}

void shape_runs(gp_runes_t vrunes, gp_run_t *runs, uint32_t len,
                uint32_t font_size)
{
	uint32_t i = 0;
	while (i < len) {
		hb_font_t *font = hb_font_create(runs[i].font->hb_face);

		// TODO: hinting will be done if hb_font_set_ppem and hb_font_set_ptem (coretext only)
		hb_font_set_scale(font, GP_SHAPE_SCALE, GP_SHAPE_SCALE);
		hb_font_set_ppem(font, font_size, font_size);

		hb_buffer_t *buf = hb_buffer_create();
		//TODO: Add context from prior and next run for better shaping.
		// Doesnt look like we can share context between buffers.
		uint32_t run_len = runs[i].end - runs[i].start;
		hb_buffer_add_codepoints(buf, &vrunes.data[runs[i].start], run_len, 0,
		                         run_len);
		hb_segment_properties_t props = {
		        .direction = runs[i].level % 2 ? HB_DIRECTION_RTL
		                                       : HB_DIRECTION_LTR,
		        .script = hb_script_from_iso15924_tag((hb_tag_t)runs[i].script),
		        //TODO: Can we do better than guessing from locale?
		        .language = hb_language_get_default(),
		};
		//TODO: Use visual order instead?
		if (props.direction == HB_DIRECTION_RTL) {
			hb_buffer_reverse(buf);
		}
		hb_buffer_set_segment_properties(buf, &props);

		// Features?
		hb_shape(font, buf, NULL, 0);
		hb_font_destroy(font);
		runs[i].glyphs = buf;
		i++;
	}
}

bool gp_analyze(gp_runes_t runes, struct gp_face_set_t faces, const char *lang,
                gp_run_t **runs_out, uint32_t *len, uint32_t font_size)
{
	UNUSED(lang);
	assert(runes.len < 4096); // I dont want to malloc.
	uint32_t vstr[4096];
	FriBidiLevel embedding[4096];
	FriBidiParType base = FRIBIDI_PAR_LTR;
	if (!fribidi_log2vis(runes.data, runes.len, &base, vstr, NULL, NULL,
	                     embedding)) {
		return false;
	}
	gp_runes_t vrunes = {vstr, runes.len};

	gp_run_t *runs;
	uint32_t runs_len;
	gp_itemize(vrunes, faces, embedding, &runs, &runs_len);

	shape_runs(vrunes, runs, runs_len, font_size);

	*runs_out = runs;
	*len = runs_len;
	return true;
}

void gp_utf8_to_runes(const char *utf8, uint32_t len, uint32_t dst_cap,
                      uint32_t *dst, uint32_t *dst_len)
{
	UNUSED(dst_cap);
	int char_set_num = fribidi_parse_charset("UTF-8");
	*dst_len = fribidi_charset_to_unicode(char_set_num, utf8, len, dst);
}

// hb drawing functions
void draw_move_to_(hb_position_t x, hb_position_t y, void *user_data)
{
	cairo_move_to((cairo_t *)user_data, x, y);
}

void draw_line_to_(hb_position_t x, hb_position_t y, void *user_data)
{
	cairo_line_to((cairo_t *)user_data, x, y);
}

void draw_quad_to_(hb_position_t c1x, hb_position_t c1y, hb_position_t x,
                   hb_position_t y, void *user_data)
{
	cairo_t *cr = (cairo_t *)user_data;
	// Cairo only uses cubic, so lets elevate the quadratic.
	double x0 = 0, y0 = 0;
	double x1 = c1x, y1 = c1y;
	double x2 = x, y2 = y;
	cairo_get_current_point(cr, &x0, &y0);
	cairo_curve_to(cr, 2.0 / 3.0 * x1 + 1.0 / 3.0 * x0,
	               2.0 / 3.0 * y1 + 1.0 / 3.0 * y0,
	               2.0 / 3.0 * x1 + 1.0 / 3.0 * x2,
	               2.0 / 3.0 * y1 + 1.0 / 3.0 * y2, x2, y2);
}

void draw_cubic_to_(hb_position_t c1x, hb_position_t c1y, hb_position_t c2x,
                    hb_position_t c2y, hb_position_t x, hb_position_t y,
                    void *user_data)
{
	cairo_curve_to((cairo_t *)user_data, c1x, c1y, c2x, c2y, x, y);
}

void draw_close_path_(void *user_data)
{
	cairo_close_path((cairo_t *)user_data);
}

// height,width,stride in bits.
// Transforms packed sbit into a cairo bitmap with given stride.
void sbit_to_bitmap(const uint8_t *sbit, uint8_t *bitmap, uint32_t width,
                    uint32_t height, uint32_t stride)
{
	// Verified against freetype single bit.
	for (uint32_t ps = 0, po = 0; ps < width * height;) {
		for (uint32_t pw = 0; pw < width;) {
			// sbit data starts from the high bit.
			// Cairo data starts from the low bit.
			uint32_t sbi = (ps + pw) / 8;               // sbit buffer
			uint32_t sbo = 7 - (ps + pw) % 8;           // sbit buffer
			uint32_t bi = (po + pw) / 8;                // bitmap buffer
			uint32_t bo = (po + pw) % 8;                // bitmap buffer
			uint8_t pixel = !!((1 << sbo) & sbit[sbi]); // reduce to 1
			bitmap[bi] |= pixel << bo;

			pw += 1;
		}
		ps += width;
		po += stride;
	}
}

// Convert from font space back to pixel space for masking in bitmaps.
void to_aligned_pixel_space(cairo_t *cr)
{
	double dx = 1, dy = 1;
	// this will unflip our Y as well to place bitmaps in the right orientation.
	cairo_device_to_user_distance(cr, &dx, &dy);
	cairo_scale(cr, dx, dy);
	// Align to nearest pixel
	double x = 0, y = 0;
	cairo_user_to_device(cr, &x, &y);
	x = round(x);
	y = round(y);
	cairo_device_to_user(cr, &x, &y);
	cairo_translate(cr, x, y);
}

// Convert align font space origin to nearest pixel
void to_aligned_font_space(cairo_t *cr)
{
	// Align to nearest pixel
	double x = 0, y = 0;
	cairo_user_to_device(cr, &x, &y);
	x = round(x);
	y = round(y);
	cairo_device_to_user(cr, &x, &y);
	cairo_translate(cr, x, y);
}

void try_draw_glyph(cairo_t *cr, hb_font_t *font, double x, double y,
                    hb_codepoint_t glyph, hb_glyph_position_t gp)
{
	cairo_save(cr);
	// flip y of font drawing to match 0,0 top,left of cairo.
	cairo_scale(cr, 1.0 / GP_SHAPE_SCALE, -1.0 / GP_SHAPE_SCALE);
	cairo_translate(cr, x + gp.x_offset, y + gp.y_offset);

	// printf("glyph(%d) at: (%f,%f)\n", glyph_info[g].codepoint, x,
	//    y);
	// printf("glyph off: (%d,%d) glyph adv: (%d,%d)\n",
	// glyph_pos[g].x_offset, glyph_pos[g].y_offset,
	// glyph_pos[g].x_advance, glyph_pos[g].y_advance);

	// Patched harfbuzz for EBDT support.
#ifdef HB_HAS_BITMAP_SUPPORT
	uint8_t bitmap_buf[1024] = {0}; // hold expanded sbits for cairo.
	// in bits.
	uint32_t width = 0, height = 0, depth = 0, data_len = 0;
	hb_blob_t *bitmap = hb_ot_color_glyph_reference_bitmap(font, glyph, &depth,
	                                                       &width, &height);
	const uint8_t *data = (const uint8_t *)hb_blob_get_data(bitmap, &data_len);

	uint32_t stride = cairo_format_stride_for_width(CAIRO_FORMAT_A1, width);
	memset(bitmap_buf, 0, height * stride);
	sbit_to_bitmap(data, bitmap_buf, width, height, stride * 8);

	cairo_surface_t *glyph_mask = cairo_image_surface_create_for_data(
	        bitmap_buf, CAIRO_FORMAT_A1, width, height, stride);
	to_aligned_pixel_space(cr);
	cairo_mask_surface(cr, glyph_mask, 0, 0.0 - height);
	cairo_surface_destroy(glyph_mask);
	hb_blob_destroy(bitmap);
#endif

	cairo_restore(cr);
}

void gp_draw_cairo(cairo_t *cr, gp_run_t *runs, uint32_t len)
{
	uint32_t i = 0;
	double x = 0, y = 0;
	while (i < len) {
		uint32_t glen;
		hb_glyph_position_t *glyph_pos =
		        hb_buffer_get_glyph_positions(runs[i].glyphs, &glen);
		hb_glyph_info_t *glyph_info =
		        hb_buffer_get_glyph_infos(runs[i].glyphs, NULL);
		hb_font_t *font = hb_font_create(runs[i].font->hb_face);
		hb_font_set_scale(font, GP_SHAPE_SCALE, GP_SHAPE_SCALE);

		hb_draw_funcs_t *funcs = hb_draw_funcs_create();
		hb_draw_funcs_set_move_to_func(funcs, draw_move_to_);
		hb_draw_funcs_set_line_to_func(funcs, draw_line_to_);
		hb_draw_funcs_set_quadratic_to_func(funcs, draw_quad_to_);
		hb_draw_funcs_set_cubic_to_func(funcs, draw_cubic_to_);
		hb_draw_funcs_set_close_path_func(funcs, draw_close_path_);

		hb_blob_t *ebdt = hb_face_reference_table(runs[i].font->hb_face,
		                                          HB_TAG('E', 'B', 'D', 'T'));
		if (hb_blob_get_length(ebdt) > 0) {
			// if (false) {
			cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
			for (uint32_t g = 0; g < glen; g++) {
				uint32_t size = 64;
				hb_codepoint_t decomposed[64] = {0};
				hb_position_t offsets[128] = {0};
				if ((size = hb_ot_color_glyph_decompose_bitmap(
				             font, glyph_info[g].codepoint, size, decomposed,
				             offsets)) != 0) {
					printf("Decomposed %d into %d components\n",
					       glyph_info[g].codepoint, size);
					for (uint32_t i = 0; i < size; i++) {
						printf("%d ", decomposed[i]);
						try_draw_glyph(cr, font, x + offsets[i * 2],
						               y + offsets[i * 2 + 1], decomposed[i],
						               glyph_pos[g]);
					}
				} else {
					printf("Bitmap didnt decompose\n");
					try_draw_glyph(cr, font, x, y, glyph_info[g].codepoint,
					               glyph_pos[g]);
				}
				x += glyph_pos[g].x_advance;
				y += glyph_pos[g].y_advance;
			}
		} else {
			cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
			for (uint32_t g = 0; g < glen; g++) {
				cairo_save(cr);
				// flip y of font drawing to match 0,0 top,left of cairo.
				cairo_scale(cr, 1.0 / GP_SHAPE_SCALE, -1.0 / GP_SHAPE_SCALE);
				cairo_translate(cr, x + glyph_pos[g].x_offset,
				                y + glyph_pos[g].y_offset);

				to_aligned_font_space(cr);
				hb_font_draw_glyph(font, glyph_info[g].codepoint, funcs, cr);
				// printf("glyph(%d) at: (%f,%f)\n", glyph_info[g].codepoint, x,
				//    y);
				// printf("glyph off: (%d,%d) glyph adv: (%d,%d)\n",
				// glyph_pos[g].x_offset, glyph_pos[g].y_offset,
				// glyph_pos[g].x_advance, glyph_pos[g].y_advance);
				x += glyph_pos[g].x_advance;
				y += glyph_pos[g].y_advance;
				cairo_restore(cr);
			}
			cairo_fill(cr);
		}

		hb_font_destroy(font);
		hb_draw_funcs_destroy(funcs);
		i++;
	}
}

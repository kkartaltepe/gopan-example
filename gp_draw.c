// Used for bitmap extension.
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

// hb drawing functions
void draw_move_to_(hb_draw_funcs_t *draw_funcs, void *draw_data,
                   hb_draw_state_t *draw_state, float x, float y,
                   void *user_data)
{
	(void)draw_funcs;
	(void)draw_state;
	(void)user_data;
	cairo_move_to((cairo_t *)draw_data, x, y);
}

void draw_line_to_(hb_draw_funcs_t *draw_funcs, void *draw_data,
                   hb_draw_state_t *draw_state, float x, float y,
                   void *user_data)
{
	(void)draw_funcs;
	(void)draw_state;
	(void)user_data;
	cairo_line_to((cairo_t *)draw_data, x, y);
}

void draw_cubic_to_(hb_draw_funcs_t *draw_funcs, void *draw_data,
                    hb_draw_state_t *draw_state, float c1x, float c1y,
                    float c2x, float c2y, float x, float y, void *user_data)
{
	(void)draw_funcs;
	(void)draw_state;
	(void)user_data;
	cairo_curve_to((cairo_t *)draw_data, c1x, c1y, c2x, c2y, x, y);
}

void draw_close_path_(hb_draw_funcs_t *draw_funcs, void *draw_data,
                      hb_draw_state_t *draw_state, void *user_data)
{
	(void)draw_funcs;
	(void)draw_state;
	(void)user_data;
	cairo_close_path((cairo_t *)draw_data);
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

// Line up the units of the current space with device pixels.
void align_pixels(cairo_t *cr)
{
	// Align to nearest pixel
	double x = 0, y = 0;
	cairo_user_to_device(cr, &x, &y);
	x = round(x);
	y = round(y);
	cairo_device_to_user(cr, &x, &y);
	cairo_translate(cr, x, y);
}

// Convert current space to pixels for masking in bitmaps.
void to_pixel_space(cairo_t *cr)
{
	double dx = 1, dy = 1;
	// this will unflip our Y as well to place bitmaps in the right orientation.
	cairo_device_to_user_distance(cr, &dx, &dy);
	cairo_scale(cr, dx, dy);
}

// Align baselines in the m-space
void align_baseline(cairo_t *cr, hb_font_t *font)
{
	hb_font_extents_t exts = {0};
	hb_font_get_h_extents(font, &exts);
	cairo_translate(cr, 0.0, -exts.line_gap);
}

// Draw metrics, in the m-space
void draw_metrics(cairo_t *cr, hb_font_t *font)
{
	cairo_save(cr);
	hb_font_extents_t exts = {0};
	hb_font_get_h_extents(font, &exts);

	cairo_set_line_width(cr, 100.0);
	cairo_set_source_rgb(cr, 1.0, 1.0, 0.0);
	cairo_move_to(cr, 0.0, 0.0);
	cairo_line_to(cr, GP_SHAPE_SCALE, GP_SHAPE_SCALE);
	cairo_stroke(cr);

	cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
	cairo_move_to(cr, 0.0, exts.descender);
	cairo_line_to(cr, GP_SHAPE_SCALE, exts.descender);
	cairo_stroke(cr);

	cairo_set_source_rgb(cr, 0.0, 1.0, 0.0);
	cairo_move_to(cr, 0.0, exts.ascender);
	cairo_line_to(cr, GP_SHAPE_SCALE, exts.ascender);
	cairo_stroke(cr);

	cairo_set_source_rgb(cr, 0.0, 0.0, 1.0);
	cairo_move_to(cr, 0.0, exts.line_gap);
	cairo_line_to(cr, GP_SHAPE_SCALE, exts.line_gap);
	cairo_stroke(cr);

	cairo_restore(cr);
}

void try_draw_bitmap(cairo_t *cr, hb_font_t *font, hb_glyph_info_t *glyph_info)
{
	// Patched harfbuzz for EBDT support.
#ifdef HB_HAS_BITMAP_SUPPORT
	hb_codepoint_t decomposed[64] = {0};
	hb_position_t offsets[128] = {0};
	uint32_t size = 64;
	if ((size = hb_ot_color_glyph_decompose_bitmap(font, glyph_info->codepoint,
	                                               size, decomposed,
	                                               offsets)) == 0) {
		size = 1;
		decomposed[0] = glyph_info->codepoint
	}

	uint8_t bitmap_buf[1024] = {0}; // hold expanded sbits for cairo.
	// in bits.
	for (uint32_t i = 0; i < size; i++) {
		cairo_save(cr);

		hb_codepoint_t glyph = decomposed[i];
		uint32_t width = 0, height = 0, depth = 0, data_len = 0;
		hb_blob_t *bitmap = hb_ot_color_glyph_reference_bitmap(
		        font, glyph, &depth, &width, &height);
		const uint8_t *data =
		        (const uint8_t *)hb_blob_get_data(bitmap, &data_len);
		if (data_len == 0) {
			return; // woops
		}
		// printf("bitmap h:%d w:%d,d:%d,l:%d\n", height, width, depth, data_len);

		uint32_t stride = cairo_format_stride_for_width(CAIRO_FORMAT_A1, width);
		assert(height * stride < 1024);
		memset(bitmap_buf, 0, height * stride);
		sbit_to_bitmap(data, bitmap_buf, width, height, stride * 8);

		cairo_surface_t *glyph_mask = cairo_image_surface_create_for_data(
		        bitmap_buf, CAIRO_FORMAT_A1, width, height, stride);

		// TODO: simplify with ppem?
		// rescale in case pixels are larger than the expected height.
		double dx = 1, dy = -1.0 * height;
		cairo_device_to_user_distance(cr, &dx, &dy);
		dy = GP_SHAPE_SCALE / dy;
		// dy = 1.0;
		to_pixel_space(cr);
		cairo_translate(cr, offsets[i * 2], offsets[i * 2 + 1]);
		if (dy < 1.0) {
			cairo_scale(cr, dy, dy);
		}
		cairo_translate(cr, 0, -1.0 * height * (dy < 1.0 ? dy : 1.0));
		// cairo_mask_surface(cr, glyph_mask, 0, -s * (height ));
		cairo_mask_surface(cr, glyph_mask, 0, 0);
		cairo_surface_destroy(glyph_mask);
		hb_blob_destroy(bitmap);

		cairo_restore(cr);
	}
#else
	(void)cr;
	(void)font;
	(void)glyph_info;
#endif
}

cairo_status_t gp_cairo_blob_read(void *user_data, unsigned char *data,
                                  unsigned int length)
{
	hb_blob_t *blob = (hb_blob_t *)user_data;
	uint64_t *offset = (uint64_t *)hb_blob_get_user_data(
	        blob, (hb_user_data_key_t *)"cairo_png_cursor");
	unsigned int blob_len = hb_blob_get_length(blob);
	const char *blob_data = hb_blob_get_data(blob, &blob_len);
	memcpy(data, blob_data + *offset, length);
	*offset += length;
	return CAIRO_STATUS_SUCCESS;
}

void gp_draw_cairo(cairo_t *cr, gp_run_t *runs, uint32_t len)
{
	uint32_t i = 0;
	double x = 0, y = 0;
	hb_draw_funcs_t *funcs = hb_draw_funcs_create();
	hb_draw_funcs_set_move_to_func(funcs, draw_move_to_, NULL, NULL);
	hb_draw_funcs_set_line_to_func(funcs, draw_line_to_, NULL, NULL);
	hb_draw_funcs_set_cubic_to_func(funcs, draw_cubic_to_, NULL, NULL);
	hb_draw_funcs_set_close_path_func(funcs, draw_close_path_, NULL, NULL);

	while (i < len) {
		hb_face_t *face = runs[i].font->hb_face;
		hb_blob_t *ebdt =
		        hb_face_reference_table(face, HB_TAG('E', 'B', 'D', 'T'));
		uint32_t glen;
		hb_glyph_position_t *glyph_pos =
		        hb_buffer_get_glyph_positions(runs[i].glyphs, &glen);
		hb_glyph_info_t *glyph_info =
		        hb_buffer_get_glyph_infos(runs[i].glyphs, NULL);
		hb_font_t *font = hb_font_create(face);
		hb_font_set_scale(font, GP_SHAPE_SCALE, GP_SHAPE_SCALE);
		hb_font_set_ppem(font, runs[i].ppem, runs[i].ppem);

		cairo_save(cr);
		cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
		// flip y of font drawing to match 0,0 top,left of cairo.
		cairo_scale(cr, 1.0 / GP_SHAPE_SCALE, -1.0 / GP_SHAPE_SCALE);

		for (uint32_t g = 0; g < glen; g++) {
			cairo_save(cr);
			cairo_translate(cr, x + glyph_pos[g].x_offset,
			                y + glyph_pos[g].y_offset);
			align_baseline(cr, font);
			align_pixels(cr);

			if (hb_blob_get_length(ebdt) > 0) {
				cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
				draw_metrics(cr, font);
				try_draw_bitmap(cr, font, &glyph_info[g]);
			} else if (hb_ot_color_has_png(face)) {
				uint64_t png_cursor = 0;
				hb_blob_t *png_hb = hb_ot_color_glyph_reference_png(
				        font, glyph_info[g].codepoint);
				hb_blob_set_user_data(png_hb,
				                      (hb_user_data_key_t *)"cairo_png_cursor",
				                      &png_cursor, NULL, true);
				cairo_surface_t *png_cairo =
				        cairo_image_surface_create_from_png_stream(
				                gp_cairo_blob_read, png_hb);
				if (cairo_surface_status(png_cairo) != CAIRO_STATUS_SUCCESS) {
					printf("Failed to load png surface: 0x%X\n",
					       cairo_surface_status(png_cairo));
				} else {
					draw_metrics(cr, font);
					cairo_scale(cr, (float)runs[i].ppem,
					            -1.0 * (float)runs[i].ppem);
					cairo_set_source_surface(
					        cr, png_cairo, 0.0,
					        -1.0 * cairo_image_surface_get_height(png_cairo));
					cairo_paint(cr);
				}

				cairo_surface_destroy(png_cairo);
				hb_blob_destroy(png_hb);
			} else {
				draw_metrics(cr, font);
				cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
				hb_font_get_glyph_shape(font, glyph_info[g].codepoint, funcs,
				                        cr);
				cairo_fill(cr);
			}
			cairo_restore(cr);

			x += glyph_pos[g].x_advance;
			y += glyph_pos[g].y_advance;
		}

		cairo_restore(cr);
		hb_font_destroy(font);
		i++;
	}
	hb_draw_funcs_destroy(funcs);
}

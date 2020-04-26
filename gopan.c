#include <fribidi.h>
#include <hb.h>
#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>
#include <fontconfig/fontconfig.h>

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "gp_props.h"

#define UNUSED(x) (void)(x)

typedef struct gp_runes {
	uint32_t *data;
	uint32_t len;
} gp_runes;

typedef struct gp_run {
	uint32_t start;
	uint32_t end;

	int16_t level; // embedding level. Even values are LTR, odd values are RTL.
	enum gp_script script;
	enum gp_width width;
	FcPattern *font;
	hb_buffer_t *glyphs;
} gp_run;

typedef struct gp_run_iter {
	uint32_t start;
	uint32_t at;

	int16_t level;
	enum gp_script script;
	enum gp_width width;
	FcPattern *font;
	uint32_t font_pri; // priority of current font

} gp_run_iter;

char *print_font(FcPattern *font)
{
	FcObjectSet *prop_filter = FcObjectSetCreate();
	// FcObjectSetAdd(prop_filter, "lang");
	FcObjectSetAdd(prop_filter, "family");
	FcObjectSetAdd(prop_filter, "size");
	FcPattern *font_small = FcPatternFilter(font, prop_filter);

	FcChar8 *s = FcNameUnparse(font_small);
	FcPatternDestroy(font_small);
	FcObjectSetDestroy(prop_filter);
	return (char *)s;
}

void print_fonts(FcFontSet *fs)
{
	// https://www.freedesktop.org/software/fontconfig/fontconfig-devel/x19.html
	FcObjectSet *prop_filter = FcObjectSetCreate();
	FcObjectSetAdd(prop_filter, "lang");
	FcObjectSetAdd(prop_filter, "family");
	FcObjectSetAdd(prop_filter, "size");
	for (int j = 0; fs && j < fs->nfont; j++) {
		FcPattern *font = FcPatternFilter(fs->fonts[j], prop_filter);
		FcChar8 *s = FcNameUnparse(font);
		printf("%s\n", s);
		FcStrFree(s);
		FcPatternDestroy(font);
	}
	FcObjectSetDestroy(prop_filter);
}

bool pattern_has_rune(FcPattern *font, uint32_t rune)
{
	FcCharSet *cs;
	FcPatternGetCharSet(font, FC_CHARSET, 0, &cs);
	return FcCharSetHasChar(cs, rune);
}

// Out put the highest priority font that has this glyph and its priority.
FcPattern *choose_font_for(uint32_t rune, FcFontSet *fs, uint32_t *priority)
{
	for (int j = 0; fs && j < fs->nfont; j++) {
		if (pattern_has_rune(fs->fonts[j], rune)) {
			if (priority)
				*priority = (uint32_t)j;
			return fs->fonts[j];
		}
		/*
				char *font_name = print_font(fs->fonts[j]);
				printf("%s missing %04x\n", font_name, rune);
				FcStrFree((FcChar8 *)font_name);
				*/
	}
	printf("Failed to find matching font in %d\n", fs->nfont);
	if (priority) // hope you dont have more than 4b fonts.
		*priority = 0xFFFFFFFF;
	return NULL;
}

FcFontSet *load_fonts(FcConfig *config, char *pattern, bool with_color)
{

	FcResult result; // maybe someone uses this sometimes or something.
	FcPattern *pat = FcNameParse((FcChar8 *)pattern);
	if (!pat) {
		printf("Woops failed to parse pattern: %s\n", pattern);
		return NULL;
	}
	if (with_color) { // for emoji selectors we want another fs with colors to prioritize by.
		FcPatternAddBool(pat, "color", with_color);
	}
	FcConfigSubstitute(config, pat, FcMatchPattern);
	FcDefaultSubstitute(pat);

	// https://www.freedesktop.org/software/fontconfig/fontconfig-devel/fcfontsort.html
	// Do the pattern search based on search. Optionally return coverage.
	// returns underspecified fonts which must be completed via
	// FcFontRendererPrepare. Some people only use fontcofig for search
	// and impl custom searching like browsers.
	FcFontSet *search_pats = FcFontSort(config, pat, FcTrue /* trim */,
	                                    0 /* coverage out */, &result);
	if (!search_pats || search_pats->nfont == 0) {
		printf("Woops no fonts installed?\n");
		return NULL;
	}

	printf("Found %d fonts\n", search_pats->nfont);
	FcFontSet *fs = FcFontSetCreate();
	for (int j = 0; j < search_pats->nfont; j++) {
		FcPattern *font_pattern =
		        FcFontRenderPrepare(config, pat, search_pats->fonts[j]);
		if (font_pattern)
			FcFontSetAdd(fs, font_pattern);
	}
	FcFontSetSortDestroy(search_pats);

	return fs;
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

void gp_itemize(gp_runes runes, FcFontSet *fs, FcFontSet *fs_color,
                FriBidiLevel *levels, gp_run **runs_out, uint32_t *len)
{
	// TODO: maintain paired chars (paren/quotes/etc)? Prefering higher priority fonts mostly fixed this.

	gp_run *runs = malloc(sizeof(gp_run) * 256);
	gp_run_iter iter = {0};
	iter.width = gp_rune_width(runes.data[0]);
	iter.script = gp_rune_script(runes.data[0]);
	iter.level = levels[0];
	if (!is_space(runes.data[0])) {
		iter.font = choose_font_for(runes.data[0], fs, &iter.font_pri);
	}
	iter.at = 1;

	size_t r = 0;
	for (; iter.at < runes.len; iter.at++) {
		bool changed = false;
		uint32_t rune = runes.data[iter.at];
		// Just dont break runs on whitespace.
		// TODO: handle (emoji) variant selectors
		if (is_space(rune) || is_variant_sel(rune)) {
			continue;
		}

		if (iter.font == NULL) {
			// we delayed choosing font until non-space so dont mark changed.
			iter.font = choose_font_for(rune, fs, &iter.font_pri);
		}

		enum gp_width width = gp_rune_width(rune);
		changed |=
		        (iter.width == GP_WIDTH_AMBIGUOUS &&
		         !(width == GP_WIDTH_AMBIGUOUS || width == GP_WIDTH_NEUTRAL));
		if (changed) {
			printf("width changed %d vs %d\n", iter.width, width);
		}

		FcPattern *font = NULL;
		uint32_t font_pri = 0xFFFFFFFF;
		// Dont break for whitespace, this also helps with joiners.
		if (iter.font != NULL) {
			FcPattern *font_test = choose_font_for(rune, fs, &font_pri);
			if (font_test != NULL && ((font_pri < iter.font_pri) ||
			                          !pattern_has_rune(iter.font, rune))) {
				changed |= true;
				font = font_test;
				printf("font changed\n");
			}
		}

		enum gp_script script = gp_rune_script(rune);
		changed |= iter.script != script;
		if (iter.script != script) {
			printf("script changed\n");
		}

		int16_t level = levels[iter.at];
		changed |= iter.level != level;
		if (iter.level != level) {
			printf("level changed\n");
		}

		if (changed) {
			runs[r].start = iter.start;
			runs[r].end = iter.at;
			runs[r].script = iter.script;
			runs[r].width = iter.width;
			runs[r].level = iter.level;
			if (iter.font == NULL) {
				printf("run had no font???\n");
			}
			runs[r].font = iter.font;

			iter.start = iter.at;
			iter.width = width;
			iter.script = script;
			iter.level = level;
			iter.font = font;
			iter.font_pri = font_pri;
			if (font == NULL && !is_space(rune)) {
				iter.font = choose_font_for(rune, fs, &iter.font_pri);
			}
			r++;
		}
	}
	runs[r].start = iter.start;
	runs[r].end = iter.at;
	runs[r].script = iter.script;
	runs[r].width = iter.width;
	runs[r].level = iter.level;
	if (iter.font == NULL) {
		printf("run had no font???\n");
	}
	runs[r].font = iter.font;
	r++;

	*runs_out = runs;
	*len = r;
}

void shape_runs(gp_runes vrunes, gp_run *runs, uint32_t len)
{
	uint32_t i = 0;
	while (i < len) {
		// load font tables (uses internal hb-ot functions)
		// should use ft to share with cairo pathing
		char *file;
		FcPatternGetString(runs[i].font, FC_FILE, 0, (FcChar8 **)&file);
		hb_blob_t *fileblob = hb_blob_create_from_file(file);
		hb_face_t *face = hb_face_create(fileblob, 0);
		hb_font_t *font = hb_font_create(face);
		// Set font size during shaping, for appropriate glyph advances.
		// add subpixel scaling factor on top since hb is integer based.
		double size;
		FcPatternGetDouble(runs[i].font, FC_PIXEL_SIZE, 0, &size);
		double size_x = size, size_y = size;

		// typically provided by 10-scale-bitmap-fonts.conf, maybe
		// other fonts will have matrix factors as well. ignore
		// "scalable" during layout, stop using bitmap text fonts.
		FcMatrix *scale_mat;
		if (FcPatternGetMatrix(runs[i].font, FC_MATRIX, 0, &scale_mat) ==
		    FcResultMatch) {
			if (scale_mat->xy != 0.0 || scale_mat->yx != 0.0) {
				printf("Uh-oh, shear/rotate matrix detected. Rendering probably going wrong.\n");
			}
			size_x *= scale_mat->xx;
			size_y *= scale_mat->yy;
		}

		hb_font_set_scale(font, size_x * 2048, size_y * 2048);

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
		hb_face_destroy(face);
		hb_blob_destroy(fileblob);
		// FcStrFree((FcChar8 *)file);
		runs[i].glyphs = buf;
		i++;
	}
}

void draw_cairo(gp_run *runs, uint32_t len)
{
	UNUSED(runs);
	UNUSED(len);
	cairo_surface_t *bitmap =
	        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 600, 64);
	cairo_t *cr = cairo_create(bitmap);

	uint32_t i = 0;
	double x = 0.0, y = 48.0;
	while (i < len) {
		cairo_font_face_t *face =
		        cairo_ft_font_face_create_for_pattern(runs[i].font);
		cairo_set_font_face(cr, face);
		double size;
		FcPatternGetDouble(runs[i].font, FC_PIXEL_SIZE, 0, &size);
		cairo_matrix_t font_mat = {size, 0, 0, size, 0, 0};

		// typically provided by 10-scale-bitmap-fonts.conf, maybe
		// other fonts will have matrix factors as well. Only use this
		// if the font is marked "scalable".
		FcMatrix *scale_mat;
		FcBool scalable;
		if (FcPatternGetMatrix(runs[i].font, FC_MATRIX, 0, &scale_mat) ==
		            FcResultMatch &&
		    (FcPatternGetBool(runs[i].font, "scalable", 0, &scalable) ==
		             FcResultMatch &&
		     scalable)) {
			if (scale_mat->xy != 0.0 || scale_mat->yx != 0.0) {
				printf("Uh-oh, shear/rotate matrix detected. Rendering probably going wrong.\n");
			}
			font_mat.xx *= scale_mat->xx;
			font_mat.yy *= scale_mat->yy;
		}
		cairo_set_font_matrix(
		        cr,
		        &font_mat); // must match shaping size, no scaling factor since cairo isnt integer based.

		uint32_t glen;
		hb_glyph_position_t *glyph_pos =
		        hb_buffer_get_glyph_positions(runs[i].glyphs, &glen);
		hb_glyph_info_t *glyph_info =
		        hb_buffer_get_glyph_infos(runs[i].glyphs, NULL);
		cairo_glyph_t *draw_glyph = malloc(sizeof(cairo_glyph_t) * glen);
		uint32_t g = 0;
		while (g < glen) {
			draw_glyph[g].index = glyph_info[g].codepoint;
			draw_glyph[g].x = x + glyph_pos[g].x_offset / 2048.0;
			draw_glyph[g].y = y + glyph_pos[g].y_offset / 2048.0;
			x += glyph_pos[g].x_advance / 2048.0;
			y += glyph_pos[g].y_advance / 2048.0;
			g++;
		}
		cairo_show_glyphs(cr, draw_glyph, glen);
		// cairo_fill(cr);
		i++;
	}

	if (cairo_surface_write_to_png(bitmap, "out.png") != CAIRO_STATUS_SUCCESS) {
		printf("Woops failed to write out png");
	}
	cairo_destroy(cr);
	cairo_surface_destroy(bitmap);
}

int main(int argc, char *argv[])
{
	if (argc != 3) {
		printf("Invalid arguments.\nCall with Fontconfig pattern and text to render.\nE.g. gopan \"sans-22:weight=10\" \"hello こんにちは 你好 مرحبا שלום ဟယ်လို 👨‍🦳👶👅👀🇹🇼🅱\"\n");
		exit(1);
	}
	FcConfig *config = FcConfigCreate();
	if (FcConfigParseAndLoad(config, NULL, FcTrue) != FcTrue) {
		printf("Failed to load fontconfig\n");
		return -1;
	}
	// Why cant we just have a real context...
	FcConfigSetCurrent(config);
	FcConfigBuildFonts(config);
	FcFontSet *fs = load_fonts(config, argv[1], false);
	FcFontSet *fs_color = load_fonts(config, argv[1], true);

	int32_t slen = strlen(argv[2]);
	FriBidiLevel embedding[256];
	FriBidiChar vstr[256], lstr[256];
	FriBidiParType base = FRIBIDI_PAR_LTR;
	int char_set_num = fribidi_parse_charset("UTF-8");

	uint32_t len =
	        fribidi_charset_to_unicode(char_set_num, argv[2], slen, lstr);
	if (!fribidi_log2vis(lstr, len, &base, vstr, NULL, NULL, embedding)) {
		return 1;
	}
	gp_runes vrunes = {vstr, len};

	uint32_t r_len;
	gp_run *runs;
	gp_itemize(vrunes, fs_color, fs_color, embedding, &runs, &r_len);

	printf("runs: %d\n", r_len);
	uint32_t i = 0;
	while (i < r_len) {
		if (runs[i].font) {
			char *font_str = print_font(runs[i].font);
			printf("s(0x%04x): %d, e:%d (%s)\n", vrunes.data[runs[i].start],
			       runs[i].start, runs[i].end, font_str);
			FcStrFree((FcChar8 *)font_str);
		} else {
			printf("s: %d, e:%d (no font?)\n", runs[i].start, runs[i].end);
		}
		i++;
	}
	printf("\n");

	shape_runs(vrunes, runs, r_len);
	printf("shaped runs I guess\n");

	draw_cairo(runs, r_len);
	printf("drew runs to out.png\n");

	FcFontSetDestroy(fs);
	return 0;
}

// We use the HB_EXPERIMENTAL_API to access hb drawing functions.
#include <fribidi.h>
#include <hb.h>

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "gp.h"

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
		runs[i].ppem = font_size;
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

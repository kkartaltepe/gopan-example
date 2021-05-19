#include <hb-ot.h>
#include <hb.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "gp_ttf.h"

bool is_between(int32_t x, int32_t l, int32_t h)
{
	return x >= l && x <= h;
}

// checks each word (space separated) for target.
bool contains_icase(char *str, uint32_t slen, char *target, uint32_t tlen)
{
	char *str_end = str + slen;
	while (str < str_end) {
		if (!strncasecmp(str, target, tlen)) {
			return true;
		}
		char *next = strchr(str, ' ');
		if (next == NULL) {
			return false;
		}
		str = next + 1;
		slen = str_end - str;
	}
	return false;
}

bool get_color(hb_face_t *face)
{
	bool has_colors = false;
	char tab[][4] = {
	        {"COLR"},
	        {"CBDT"},
	        {"sbix"},
	};

	for (uint32_t i = 0; i < 3 && !has_colors; i++) {
		hb_blob_t *table = hb_face_reference_table(
		        face, HB_TAG(tab[i][0], tab[i][1], tab[i][2], tab[i][3]));
		has_colors |= hb_blob_get_length(table) != 0;
		hb_blob_destroy(table);
	}

	return has_colors;
}

bool get_scalable(hb_face_t *face)
{
	// FT2 checks for outline
	// glyf, CFF, CFF2
	// FT2 noted deprecated checks, we dont check:
	// No outlines if sbix table exists (may change?)
	// if bhed but no hhea, also no outlines.

	bool has_glyphs = false;
	char tab[][4] = {
	        {"glyf"},
	        {"CFF "},
	        {"CFF2"},
	};

	for (uint32_t i = 0; i < 3 && !has_glyphs; i++) {
		hb_blob_t *table = hb_face_reference_table(
		        face, HB_TAG(tab[i][0], tab[i][1], tab[i][2], tab[i][3]));
		has_glyphs |= hb_blob_get_length(table) != 0;
		hb_blob_destroy(table);
	}

	// FC then or's with has_color. For now lets not.
	return has_glyphs;
}

uint32_t get_spacing(hb_face_t *face, hb_set_t *codepoints)
{
	// FC reads glyph advances from all glyphs in the charmap (or until we find
	// more than 2 different advances for mono/dual spaced fonts. Chooses a size
	// ~16 pixels and allows ~3% difference in advances. (16 pixels in 26.6 is
	// 1024)

	hb_font_t *font = hb_font_create(face);
	uint32_t advance_count = 0;
	hb_position_t advances[10] = {0};
	uint32_t max_advances = 3;

	if (hb_set_get_population(codepoints) > 256) {
		// allow some missized/1.5x/3x sized glyphs for larger fonts
		// like noto mono
		max_advances = 7;
	}
	hb_codepoint_t codepoint = HB_SET_VALUE_INVALID;

	// maybe read via advances api to read more glyphs at once since we end up
	// copying.
	// Try to check only BMP or at least 50 characters, some noto fonts only
	// contain non-bmp characters.
	for (uint32_t checked = 0; hb_set_next(codepoints, &codepoint) &&
	                           (checked < 50 || codepoint < 0xFFFF) &&
	                           advance_count < max_advances;) {
		hb_codepoint_t g = 0;
		if (!hb_font_get_nominal_glyph(font, codepoint, &g)) {
			continue; // should never happen...
		}

		int32_t advance = hb_font_get_glyph_h_advance(font, g);
		if (advance == 0) {
			continue;
		}

		checked++;
		bool matched = false;
		for (uint32_t i = 0; i < advance_count; i++) {
			if (is_between(advances[i], advance * 0.97, advance * 1.03)) {
				matched = true;
			}
		}
		// Allow dual width to have triple width characters.
		if (!matched) {
			advances[advance_count++] = advance;
		}
	}

	// 1 mono, 2 dual, 3 proportional
	if (advance_count == 1) {
		return 1;
	} else if (advance_count < max_advances) {
		return 2;
	}
	return 3;
}

struct str_to_float {
	char *k;
	float v;
};

float check_for_str(struct str_to_float *v, uint32_t lenv, char *str)
{
	uint32_t slen = strlen(str);
	for (uint32_t i = 0; i < lenv; i++) {
		uint32_t klen = strlen(v[i].k); // heh...
		if (contains_icase(str, slen, v[i].k, klen)) {
			return v[i].v;
		}
	}
	return -1.0f;
}

float get_slant(char *face_style, char *face_subfamily)
{
	struct str_to_float keywords[] = {
	        {"italic", 100.0f},
	        {"kursiv", 100.0f},
	        {"oblique", 110.0f},
	};
	float s = check_for_str(keywords, 3, face_subfamily);
	if (s >= 0.0f) {
		return s;
	}
	s = check_for_str(keywords, 3, face_style);
	if (s >= 0.0f) {
		return s;
	}
	return 0.0f;
}

float get_weight(char *face_style, char *face_subfamily)
{
	struct str_to_float keywords[] = {
	        {"thin", 0.0f},         {"extralight", 40.0f},
	        {"ultralight", 40.0f},  {"demilight", 55.0f},
	        {"semilight", 55.0f},   {"light", 50.0f},
	        {"book", 75.0f},        {"regular", 80.0f},
	        {"normal", 80.0f},      {"medium", 100.0f},
	        {"demibold", 180.0f},   {"demi", 180.0f},
	        {"semibold", 180.0f},   {"extrabold", 205.0f},
	        {"superbold", 205.0f},  {"ultrabold", 205.0f},
	        {"bold", 200.0f},       {"ultrablack", 215.0f},
	        {"superblack", 215.0f}, {"extrablack", 215.0f},
	        {"ultra", 205.0f}, // as its own word
	        {"black", 210.0f},      {"heavy", 210.0f},
	};
	float w = check_for_str(keywords, 23, face_subfamily);
	if (w >= 0.0f) {
		return w;
	}
	w = check_for_str(keywords, 23, face_style);
	if (w >= 0.0f) {
		return w;
	}
	return 80.0f;
}

float get_width(char *face_style, char *face_subfamily)
{
	struct str_to_float keywords[] = {
	        {"ultracondensed", 50.0},  {"extracondensed", 63.0f},
	        {"semicondensed", 87.0f},  {"condensed", 75.0f},
	        {"normal", 100.0f},        {"semiexpanded", 113.0f},
	        {"extraexpanded", 150.0f}, {"ultraexpanded", 200.0f},
	        {"expanded", 125.0f},      {"extended", 125.0f},
	};
	float w = check_for_str(keywords, 10, face_subfamily);
	if (w >= 0.0f) {
		return w;
	}
	w = check_for_str(keywords, 10, face_style);
	if (w >= 0.0f) {
		return w;
	}
	return 100.0f;
}

bool get_ui(char *face_family)
{
	struct str_to_float keywords[] = {
	        {"ui", 1.0f},
	};
	float u = check_for_str(keywords, 1, face_family);
	return u == 1.0f;
}

uint32_t get_serif(hb_face_t *face, char *face_family)
{
	// 0 unknown, 1 sans, 2 serif
	uint32_t serif = 0;

	// OS/2 is only really filled out in noto fonts (and maybe MS fonts)
	// May be less correct then font name...
	hb_blob_t *table =
	        hb_face_reference_table(face, HB_TAG('O', 'S', '/', '2'));
	uint32_t tlen = hb_blob_get_length(table);
	if (tlen > 0 && tlen > 43) {
		const uint8_t *data = hb_blob_get_data(table, NULL);
		const uint8_t *panose = &data[32];
		if (panose[0] == 2) {
			if (panose[1] >= 1 && panose[1] <= 10) {
				serif = 2;
			}
			if (panose[1] >= 11) {
				serif = 1;
			}
		}
	}
	hb_blob_destroy(table);
	if (serif) {
		return serif;
	}

	// Check for common sans/serif notations in family name.
	// These are not perfect, as CJK fonts may not use spaces between names and
	// these notations which we will miss. And they like using n/p prefixes.
	struct str_to_float keywords[] = {
	        {"roman", 2.0f},   {"serif", 2.0f}, {"mincho", 2.0f},
	        {"pmincho", 2.0f}, {"sun", 2.0f},   {"gothic", 1.0f},
	        {"pgothic", 1.0f}, {"hei", 1.0f},   {"sans", 1.0f},
	};
	float s = check_for_str(keywords, 9, face_family);
	if (s >= 0.0f) {
		serif = (uint32_t)s;
	}
	return serif;
}

struct table_rec {
	uint32_t tag;
	uint32_t checksum;
	uint32_t offset;
	uint32_t len;
};

struct table_dir {
	uint32_t tag;
	uint16_t table_count;
	uint16_t res1;
	uint16_t res2;
	uint16_t res3;
	struct table_rec records[];
};

uint64_t compute_id(hb_face_t *face)
{
	uint64_t id = 0;
	hb_blob_t *face_data = hb_face_reference_blob(face);
	if (hb_blob_get_length(face_data) != 0) {
		uint32_t len = 0;
		// Assume things are correctly sized since harfbuzz parsed it.
		// But everything is BE.
		const char *data = hb_blob_get_data(face_data, &len);
		uint32_t tag =
		        (data[0] << 24) + (data[1] << 16) + (data[2] << 8) + (data[3]);
		if (tag == HB_TAG(0, 1, 0, 0) || tag == HB_TAG('O', 'T', 'T', 'O')) {
			const struct table_dir *tables = (const struct table_dir *)data;
			uint16_t table_count = (data[4] << 8) + (data[5]);
			for (int i = 0; i < table_count; i++) {
				id = id * 37 + tables->records[i].tag;
				id = id * 37 + tables->records[i].checksum;
				id = id * 37 + tables->records[i].len;
			}
		} else if (len >= 500) {
			// Just hash the first 500 bytes
			// This should cover up to 18 tables and be small enough that its
			// hard to construct a valid font smaller than this.
			for (int i = 0; i < 500; i++) {
				id = id * 37 + data[i];
			}
		} else {
			// printf("Not ttf and small? %x\n", tag);
			// Oh well.
		}
	}
	hb_blob_destroy(face_data);

	return id;
}

// Compare how close `other` is to the desired font `want`.
// Returns an array of uint8 that can be used to sort how close
// other fonts are to the wanted font.
uint64_t gp_compare_fonts(struct gp_face_t *want, struct gp_face_t *other)
{
	bool color = want->color != other->color; // we want what we dont have.
	bool scalable = want->scalable == other->scalable;

	bool spacing = want->spacing == other->spacing;
	bool variable = want->variable == other->variable;
	float width_diff, weight_diff, slant_diff;
	if (other->variable) {
		// assume they cover similar spaces.
		width_diff = weight_diff = slant_diff = 0.0f;
	} else if (want->variable && !other->variable) {
		// Prefer regular fonts if a variable font was chosen. We should have
		// resolved metrics before this point probably so we can use them but
		// whatever.
		width_diff = fabs(100.0f - other->width);
		weight_diff = fabs(80.0f - other->weight);
		slant_diff = fabs(0.0f - other->slant);
	} else if (!want->variable && !other->variable) {
		width_diff = fabs(want->width - other->width);
		weight_diff = fabs(want->weight - other->weight);
		slant_diff = fabs(want->slant - other->slant);
	}
	uint8_t width = ~(uint8_t)(width_diff / 20.0f);  // 0 - 800, 40 steps.
	uint8_t weight = ~(uint8_t)(weight_diff / 5.0f); // 0 - 210, 42 steps.
	uint8_t slant = ~(uint8_t)(slant_diff / 10.0f);  // 0 - 110, 11 steps.
	bool ui = want->ui == other->ui;
	bool serif = want->serif == other->serif;

	return (color << 30) + (scalable << 29) + (spacing << 28) +
	       (variable << 27) + (width << 26) + (weight << 18) + (slant << 10) +
	       (ui << 2) + (serif << 1);
}

bool gp_face_from_file(char *filename, uint32_t face_index,
                       struct gp_face_t *out_face)
{
	hb_blob_t *want_blob = hb_blob_create_from_file(filename);
	hb_face_t *want_hb_face = hb_face_create(want_blob, face_index);
	return gp_face_from_hb_face(want_hb_face, out_face);
}

bool gp_face_from_hb_face(hb_face_t *face, struct gp_face_t *out_face)
{
	uint32_t stlen = 255;
	char family[256] = {0};
	char style[256] = {0};
	char subfamily[256] = {0};
	hb_ot_name_get_utf8(face, 1, HB_LANGUAGE_INVALID, &stlen, family);
	stlen = 255;
	hb_ot_name_get_utf8(face, 2, HB_LANGUAGE_INVALID, &stlen, style);
	stlen = 255;
	hb_ot_name_get_utf8(face, 17, HB_LANGUAGE_INVALID, &stlen, subfamily);

	out_face->hb_face = face;
	hb_set_t *coverage = hb_set_create();
	hb_face_collect_unicodes(face, coverage);
	out_face->coverage = coverage;
	out_face->id = compute_id(face);
	out_face->score = 0;
	out_face->color = get_color(face);
	out_face->scalable = get_scalable(face);
	out_face->spacing = get_spacing(face, coverage);
	out_face->variable = hb_ot_var_has_data(face);
	out_face->width = get_width(style, subfamily);
	out_face->weight = get_weight(style, subfamily);
	out_face->slant = get_slant(style, subfamily);
	out_face->ui = get_ui(family);
	out_face->serif = get_serif(face, family);

	return true;
}

bool gp_face_destroy(struct gp_face_t *face)
{
	hb_face_destroy(face->hb_face);
	return true;
}

bool gp_face_set_destroy(struct gp_face_set_t set)
{
	for (uint32_t i = 0; i < set.len; i++) {
		gp_face_destroy(&set.faces[i]);
	}
	return true;
}

int cmp_score(const void *l, const void *r)
{
	// sort is desc size by flipping scores.
	return ((struct gp_face_t *)r)->score - ((struct gp_face_t *)l)->score;
}

// sort set in place.
bool gp_sort_face_set(struct gp_face_set_t *set, struct gp_face_t *target)
{
	for (uint32_t i = 0; i < set->len; i++) {
		set->faces[i].score = gp_compare_fonts(target, &set->faces[i]);
	}

	// sort fonts
	qsort(set->faces, set->len, sizeof(struct gp_face_t), cmp_score);

	hb_set_t *total_coverage = hb_set_create();
	hb_set_set(total_coverage, target->coverage);
	// push non-covering fonts to the back.
	uint32_t fallback_faces = 0;
	for (uint32_t i = 0; i < set->len; i++) {
		// This is bone breakingly slow in harfbuzz 2.8.1
		// But we have a PR to fix it.
		if (!hb_set_is_subset(set->faces[i].coverage, total_coverage)) {
			hb_set_union(total_coverage, set->faces[i].coverage);
			// does preserving non-covering order help?
			if (fallback_faces != i) {
				struct gp_face_t swap = set->faces[i];
				set->faces[i] = set->faces[fallback_faces];
				set->faces[fallback_faces] = swap;
			}
			fallback_faces++;
		}
	}
	hb_set_destroy(total_coverage);

	set->len = fallback_faces;

	return true;
}

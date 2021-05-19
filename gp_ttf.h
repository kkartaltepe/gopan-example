#ifndef GP_HEADER_GP_TTF_H
#define GP_HEADER_GP_TTF_H

#include <hb.h>
#include <stdbool.h>
#include <stdint.h>

struct gp_face_t {
	hb_face_t *hb_face; // hb_face_t
	hb_set_t *coverage; // we need fast coverage for sorting and itemizing text
	uint64_t id;        // mix the offset table since it has checksums
	uint64_t score;     // Score for sorting
	// Face properties for comparing fonts
	bool color;
	bool scalable;   // bitmap or outlines
	uint8_t spacing; // mono, dual, proportional
	bool variable;   // variable weight,width,slant
	float width;     // condensed, regular, extended
	float weight;    // light, regular, bold
	float slant;     // regular, italic, oblique
	bool ui;
	uint8_t serif; // unknown, sans, serif
};

struct gp_face_set_t {
	uint32_t len;
	struct gp_face_t *faces;
};

bool gp_face_from_hb_face(hb_face_t *face, struct gp_face_t *out_face);
bool gp_face_from_file(char *filename, uint32_t face_index,
                       struct gp_face_t *out_face);
bool gp_face_destroy(struct gp_face_t *face);
bool gp_face_set_destroy(struct gp_face_set_t set);


uint64_t gp_compare_fonts(struct gp_face_t *want, struct gp_face_t *other);

bool gp_sort_face_set(struct gp_face_set_t *set, struct gp_face_t *target);

#endif

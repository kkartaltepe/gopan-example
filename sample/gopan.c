#include <cairo/cairo.h>
#include <gp.h>
#include <hb-ot.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

bool has_suffix(char *str, char *suffix)
{
	int len = strlen(str);
	int slen = strlen(suffix);
	if (len < slen) {
		return false;
	}
	return memcmp(str + len - slen, suffix, slen) == 0;
}

void print_font(struct gp_face_t *face)
{
	char family[256] = {0};
	char style[256] = {0};
	char subfamily[256] = {0};
	uint32_t stlen = 255;
	hb_ot_name_get_utf8(face->hb_face, 1, HB_LANGUAGE_INVALID, &stlen, family);
	stlen = 255;
	hb_ot_name_get_utf8(face->hb_face, 2, HB_LANGUAGE_INVALID, &stlen, style);
	stlen = 255;
	hb_ot_name_get_utf8(face->hb_face, 17, HB_LANGUAGE_INVALID, &stlen,
	                    subfamily);
	printf("%s:%s:%s\n", family, style, subfamily);
}

// load some known font dirs.
bool load_default_face_set(struct gp_face_set_t *set, uint32_t capacity)
{
	char *dirs[] = {"/usr/share/fonts/noto", "/usr/share/fonts/noto-cjk"};
	uint32_t num_dirs = 2;
	char filepath[512] = {0};

	for (uint32_t i = 0; i < num_dirs && set->len < capacity; i++) {
		DIR *dh = opendir(dirs[i]);
		if (dh) {
			struct dirent *de = NULL;
			while ((de = readdir(dh))) {
				if (!(de->d_type == DT_REG &&
				      (has_suffix(de->d_name, ".ttf") ||
				       has_suffix(de->d_name, ".otf") ||
				       has_suffix(de->d_name, ".ttc")))) {
					continue;
				}

				uint32_t dir_len = strlen(dirs[i]);
				memcpy(filepath, dirs[i], dir_len);
				filepath[dir_len++] = '/';
				uint32_t file_len = strlen(de->d_name);
				memcpy(&filepath[dir_len], de->d_name, file_len);

				hb_blob_t *blob = hb_blob_create_from_file(filepath);
				uint32_t fc = hb_face_count(blob);
				for (uint32_t j = 0; j < fc && set->len < capacity; j++) {
					hb_face_t *hb_face = hb_face_create(blob, i);
					struct gp_face_t *face = &set->faces[set->len++];
					gp_face_from_hb_face(hb_face, face);
				}

				memset(filepath, 0, dir_len + file_len);
			}
			closedir(dh);
		}
	}

	return true;
}

int main(int argc, char *argv[])
{
	if (argc != 3) {
		printf("Invalid arguments %d.\nCall with primary font file and text to render.\nE.g. gopan \"/usr/share/fonts/noto/NotoSans-Regular.ttf\" \"hello こんにちは 你好 مرحبا שלום ဟယ်လို 👨‍🦳👶👅👀🇹🇼🅱\"\n",
		       argc);
		exit(1);
	}

	// reserve first for chosen font.
	struct gp_face_t faces_buf[1024] = {0};
	struct gp_face_set_t faces = {0, &faces_buf[1] };
	load_default_face_set(&faces, 1024);
	gp_face_from_file(argv[1], 0, &faces_buf[0]);
	gp_sort_face_set(&faces, &faces_buf[0]);
	// re-add the main face at the front of the set.
	faces.faces = faces_buf;
	faces.len++;
	printf("Sorted into %d fallback faces\n", faces.len);


	uint32_t lstr[256];
	gp_runes_t runes = {lstr, 0};
	gp_utf8_to_runes(argv[2], strlen(argv[2]), 256, runes.data, &runes.len);

	double font_size_px = 16;
	uint32_t r_len;
	gp_run_t *runs;
	gp_analyze(runes, faces, "en-US", &runs, &r_len, font_size_px);

	printf("runs: %d\n", r_len);
	uint32_t i = 0;
	while (i < r_len) {
		if (runs[i].font) {
			printf("s(0x%04x): %d, e:%d, ", runes.data[runs[i].start],
			       runs[i].start, runs[i].end);
			print_font(runs[i].font); // ends with \n
		} else {
			printf("s: %d, e:%d (no font?)\n", runs[i].start, runs[i].end);
		}
		i++;
	}
	printf("\n");

	cairo_surface_t *bitmap =
	        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 600, 64);
	cairo_t *cr = cairo_create(bitmap);
	// set our baseline.
	cairo_translate(cr, 0, 42.0);
	// gp_draw_cairo will draw in unit space, scale to pixel size.
	cairo_scale(cr, font_size_px, font_size_px);
	gp_draw_cairo(cr, runs, r_len);

	if (cairo_surface_write_to_png(bitmap, "out.png") != CAIRO_STATUS_SUCCESS) {
		printf("Woops failed to write out png\n");
	}
	cairo_destroy(cr);
	cairo_surface_destroy(bitmap);
	printf("drew runs to out.png\n");

	gp_run_destroy(runs, r_len);
	gp_face_set_destroy(faces);
	// FcFontSetDestroy(fs);
	return 0;
}

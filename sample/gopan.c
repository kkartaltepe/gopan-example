#include <cairo/cairo.h>
#include <fontconfig/fontconfig.h>
#include <gp.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	FcFontSet *fs = gp_load_font(config, argv[1], false);
	FcFontSet *fs_color = gp_load_font(config, argv[1], true);

	uint32_t lstr[256];
	gp_runes_t runes = {lstr, 0};
	gp_utf8_to_runes(argv[2], strlen(argv[2]), 256, runes.data, &runes.len);

	uint32_t r_len;
	gp_run_t *runs;
	gp_analyze(runes, fs, fs_color, "en-US", &runs, &r_len);

	printf("runs: %d\n", r_len);
	uint32_t i = 0;
	while (i < r_len) {
		if (runs[i].font) {
			char *font_str = print_font(runs[i].font);
			printf("s(0x%04x): %d, e:%d (%s)\n", runes.data[runs[i].start],
			       runs[i].start, runs[i].end, font_str);
			FcStrFree((FcChar8 *)font_str);
		} else {
			printf("s: %d, e:%d (no font?)\n", runs[i].start, runs[i].end);
		}
		i++;
	}
	printf("\n");

	cairo_surface_t *bitmap =
	        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 600, 64);
	cairo_t *cr = cairo_create(bitmap);
	gp_draw_cairo(cr, runs, r_len);

	if (cairo_surface_write_to_png(bitmap, "out.png") != CAIRO_STATUS_SUCCESS) {
		printf("Woops failed to write out png");
	}
	cairo_destroy(cr);
	cairo_surface_destroy(bitmap);
	printf("drew runs to out.png\n");

	gp_run_destroy(runs, r_len);
	FcFontSetDestroy(fs);
	return 0;
}

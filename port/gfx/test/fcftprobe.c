/*
 * fcftprobe: what foot asks of fcft before it draws a terminal - a font by
 * fontconfig name, and a glyph from it - with fcft's own log at debug, so
 * that "failed to load primary fonts" says which step refused.
 *
 *   fcftprobe ['font name']        default 'DejaVu Sans Mono:size=14'
 *
 * Prints FCFTPROBE ok <height> <glyph w>x<h> or FCFTPROBE fail <step>.
 *
 * Built against the stack in DESTDIR (D=~/dest-evbarm64):
 *   aarch64-elf64-minix-gcc --sysroot=$D -static -o fcftprobe fcftprobe.c \
 *     -I$D/usr/include/pixman-1 -lfcft -lfontconfig -lfreetype -lexpat \
 *     -lz -lpixman-1 -lgfxcompat -lmthread -lm
 */
#include <stdio.h>
#include <stdlib.h>

#include <fcft/fcft.h>

int
main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "DejaVu Sans Mono:size=14";
	const char *names[1] = { name };
	struct fcft_font *font;
	const struct fcft_glyph *g;

	fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_DEBUG);
	if ((font = fcft_from_name(1, names, NULL)) == NULL) {
		printf("FCFTPROBE fail from_name\n");
		return 1;
	}
	printf("font: height %d, ascent %d, descent %d, max advance %d\n",
	    font->height, font->ascent, font->descent, font->max_advance.x);
	if ((g = fcft_rasterize_char_utf32(font, U'A', FCFT_SUBPIXEL_NONE)) ==
	    NULL) {
		printf("FCFTPROBE fail rasterize\n");
		return 1;
	}
	printf("FCFTPROBE ok %d %dx%d\n", font->height, g->width, g->height);
	fcft_destroy(font);
	fcft_fini();
	return 0;
}

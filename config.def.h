#ifndef CONFIG_H
#define CONFIG_H

#define JPTE_SHELL		"/bin/sh"
#define JPTE_FONT_PATH		"/usr/share/fonts/TTF/DejaVuSansMono.ttf"
#define JPTE_FONT_SIZE		16
#define JPTE_SAVE_LINES		4096
#define JPTE_TRANSPARENT	1
#define JPTE_SHADING		50
#define JPTE_CURSOR_COLOR	"#FFFFFF"
#define JPTE_CURSOR_COLORBG	"#000000"
#define JPTE_CURSOR_BLINK	1
#define JPTE_FG_COLOR		"#FFFFFF"
#define JPTE_BG_COLOR		"#000000"

/* buffer for font file, increase if needed */
static unsigned char font_buffer[1 << 19];

/* static unsigned int cols = 80; */
/* static unsigned int rows = 24; */

#endif /* !CONFIG_H */

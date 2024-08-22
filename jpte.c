#include <stdint.h>		/* uint32_t, uint8_t, etc. */
#include <stdio.h>		/* fopen(), fread(), printf() */
#include <stdlib.h>		/* exit(), malloc(), free() */
#include <string.h>		/* memset(), memcpy(), strlen() */
#include <unistd.h>		/* fork(), exec(), close() */
#include <sys/select.h>		/* select() */
#include <pty.h>		/* forkpty(), openpty() */
#include <libtsm.h>		/* tsm_vte_new(), tsm_vte_input(), tsm_screen_draw() */
#include <xcb/xcb.h>		/* xcb_connect(), xcb_create_window(), xcb_flush() */
#include <xcb/xcb_keysyms.h>	/* xcb_key_symbols_alloc(), xcb_key_symbols_get_keysym() */
#include <xcb/xproto.h>		/* xcb_create_window(), xcb_change_property(), xcb_put_image() */

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "config.h"

#ifdef DEBUG
#define debug_print(...)	fprintf(stderr, __VA_ARGS__);
#else
#define debug_print(...)	;
#endif

int ptty_fd;
struct tsm_vte *vte;
struct tsm_screen *tsm_screen;
xcb_connection_t *conn;
xcb_screen_t *screen;
xcb_window_t window;
xcb_gcontext_t gc;
xcb_key_symbols_t *keysyms;
stbtt_fontinfo font_info;

typedef struct {
	unsigned char *bitmap;
	int width, height;
	int x_offset, y_offset;
	int x_advance;
} GlyphInfo;

#define MAX_GLYPHS 128
GlyphInfo glyphs[MAX_GLYPHS];

/* Helper function to print the error and exit */
static void die(const char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}

	exit(1);
}

static void load_glyphs(void) 
{
	for (int i = 0; i < MAX_GLYPHS; ++i) {
		int width, height, x_offset, y_offset;
		unsigned char *bitmap = stbtt_GetCodepointBitmap(&font_info, 0,
				stbtt_ScaleForPixelHeight(&font_info, JPTE_FONT_SIZE),
				i, &width, &height, &x_offset, &y_offset);

		int advance_width, lsb;
		stbtt_GetCodepointHMetrics(&font_info, i, &advance_width, &lsb);

		glyphs[i].bitmap = bitmap;
		glyphs[i].width = width;
		glyphs[i].height = height;
		glyphs[i].x_offset = x_offset;
		glyphs[i].y_offset = y_offset;
		glyphs[i].x_advance = advance_width;
	}
}

/* Initialize stb_truetype and load glyphs */
static void setup_stbtt(void)
{
	FILE *font_file = fopen(JPTE_FONT_PATH, "rb");
	if (!font_file) {
		die("setup_stbtt: fopen:");
	}
	size_t font_size = fread(font_buffer, 1, sizeof(font_buffer), font_file);
	fclose(font_file);
	if (font_size <= 0) {
		die("setup_stbtt: failed to read font file");
	}
	if (!stbtt_InitFont(&font_info, font_buffer,
				stbtt_GetFontOffsetForIndex(font_buffer, 0))) {
		die("setup_stbtt: failed to initialize font");
	}
	load_glyphs();

	debug_print("setup_stbtt: done\n");
}

static void free_glyphs(void)
{
	for (int i = 0; i < MAX_GLYPHS; ++i) {
		free(glyphs[i].bitmap);
	}
}

/* Writes data to the master file descriptor and prints debug information */
static void write_cb(struct tsm_vte *vte, const char *u8, size_t len, void *data)
{
	(void)vte;
	(void)data;
	write(ptty_fd, u8, len);
	debug_print("write_cb: %.*s\n", (int)len, u8);
}

/* Feeds terminal output into the VTE */
static void handle_output(const char *buffer, size_t length)
{
	tsm_vte_input(vte, buffer, length);
	debug_print("handle_output: %.*s\n", (int)length, buffer);
}

static void draw_glyph(xcb_drawable_t drawable, xcb_gcontext_t gc, int16_t x, int16_t y, GlyphInfo *glyph)
{
	for (int j = 0; j < glyph->height; ++j) {
		for (int i = 0; i < glyph->width; ++i) {
			if (glyph->bitmap[j * glyph->width + i]) {
				xcb_point_t point = {x + i + glyph->x_offset, y + j + glyph->y_offset};
				xcb_poly_point(conn, XCB_COORD_MODE_ORIGIN, drawable, gc, 1, &point);
			}
		}
	}
	xcb_flush(conn);
}

/* Draws text on the XCB window */
static void draw_text(xcb_drawable_t drawable, xcb_gcontext_t gc, int16_t x, int16_t y, const char *text)
{
	/* xcb_image_text_8(conn, strlen(text), drawable, gc, x, y, text); */
	/* xcb_flush(conn); */
	/* debug_print("draw_text: '%s' at (%d, %d)\n", text, x, y); */
	 while (*text) {
		 int codepoint = *text++;
		 if (codepoint < MAX_GLYPHS) {
			 GlyphInfo *glyph = &glyphs[codepoint];
			 draw_glyph(drawable, gc, x, y, glyph);
			 x += glyph->x_advance;
		 }
	 }
}

/* Callback function for drawing characters */
static inline int draw_cb(struct tsm_screen *con, uint64_t id, const uint32_t *ch,
			size_t len, unsigned int width, unsigned int posx,
			unsigned int posy, const struct tsm_screen_attr *attr,
			tsm_age_t age, void *data)
{
	(void)con;
	(void)id;
	(void)len;
	(void)width;
	(void)attr;
	(void)age;
	(void)data;

	char buf[5] = {0};
	int bytes = tsm_ucs4_to_utf8(*ch, buf);
	buf[bytes] = '\0';
	if (bytes > 0 && buf[0] != '\0') {
		draw_text(window, gc, posx * 10, posy * 20, buf);
	}
	return 0;
}

/* Clears the screen and redraws it using `tsm_screen_draw` */
static void draw_screen()
{
	// TODO: only draw required area
	xcb_clear_area(conn,
			0,	/* exposures */
			window,	/* window id */
			0, 0,	/* x, y */
			screen->width_in_pixels,
			screen->height_in_pixels);

	tsm_screen_draw(tsm_screen,	/* connection */
			draw_cb,	/* draw callback */
			NULL);		/* data */

	xcb_flush(conn);
	debug_print("draw_screen: done\n");
}

/* Handles key press events, sends input to the VTE and the shell */
static void handle_key_press(xcb_key_press_event_t *kp)
{
	// TODO: handle backspace, alt, etc
	///char buf[32];
	///memset(buf, 0, sizeof(buf));
	///xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, kp->detail, 0);

	///if (keysym) {
	///	debug_print("handle_key_press: keysym %u\n", keysym);

	///	int len = snprintf(buf, sizeof(buf), "%c", keysym);
	///	buf[len] = '\0';
	///	tsm_vte_handle_keyboard(vte, buf[0], 0, 0, 0);
	///	write(ptty_fd, buf, len);

	///	debug_print("handle_key_press: %s\n", buf);
	///}
	


	// TODO: support unicode, this example is for 1 byte only characters
	xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, kp->detail, 0);
	if (!keysym) {
		debug_print("handle_key_press: unknown keysym %u\n", kp->detail);
		return;
	}

	char buf[4] = {0}; // UTF-8 encoding for single characters
	int len = 0;

	switch (keysym) {
		// problem: where to define XCB_KEYSYM_BACKSPACE, XCB_KEYSYM_RETURN
	//case XCB_KEYSYM_BACKSPACE:
	//	buf[0] = '\x08'; // ASCII backspace
	//	len = 1;
	//	break;
	//case XCB_KEYSYM_RETURN:
	//	buf[0] = '\n';
	//	len = 1;
	//	break;
	default:
		if (keysym >= 32 && keysym <= 126) {
			buf[0] = (char)keysym;
			len = 1;
		}
		break;
	}

	if (len > 0) {
		tsm_vte_handle_keyboard(vte, buf[0], 0, 0, 0);
		write(ptty_fd, buf, len);
		debug_print("handle_key_press: %.*s\n", len, buf);
	}
}

/* Processes XCB events and handles terminal output */
static void event_loop(void)
{
	fd_set fds;
	int xcb_fd = xcb_get_file_descriptor(conn);
	int max_fd = (xcb_fd > ptty_fd) ? xcb_fd : ptty_fd;
	char buf[512];
	xcb_generic_event_t *event;
	int n;

	while (1) {
		FD_ZERO(&fds);
		FD_SET(xcb_fd, &fds);
		FD_SET(ptty_fd, &fds);

		if (select(max_fd + 1, &fds, NULL, NULL, NULL) == -1) {
			perror("event_loop: select");
			break;
		}
		if (FD_ISSET(xcb_fd, &fds)) {
			while ((event = xcb_poll_for_event(conn))) {
				switch (event->response_type & ~0x80) {
				case XCB_EXPOSE:
					draw_screen();
					break;
				case XCB_KEY_PRESS:
					handle_key_press((xcb_key_press_event_t *)event);
					break;
					// TODO: mouse press
				default:
					debug_print("event_loop: unhandled event type %u\n",
							event->response_type);
					break;
				}
			}
			free(event);
		}
		if (FD_ISSET(ptty_fd, &fds)) {
			n = read(ptty_fd, buf, sizeof(buf));
			if (n <= 0) break;
			buf[n] = '\0';
			debug_print("read: %d bytes: buf '%s'\n", n, buf);
			handle_output(buf, n);
			draw_screen();
		}
	}
}

/* Initializes XCB and creates a window */
static void setup_xcb(void)
{
	conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(conn)) {
		die("setup_xcb: unable to open X display");
	}
	const xcb_setup_t *setup = xcb_get_setup(conn);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
	screen = iter.data;

	uint32_t value_list[] = {screen->black_pixel,
			XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS};

	window = xcb_generate_id(conn);
	xcb_create_window(conn,				/* connection */
			XCB_COPY_FROM_PARENT,		/* depth */
			window,				/* window id */
			screen->root,			/* parent window */
			0, 0,				/* x, y */
			800, 600,			/* width, height */
			0,				/* border_width */
			XCB_WINDOW_CLASS_INPUT_OUTPUT,	/* class */
			screen->root_visual,		/* visual */
			/* masks */
			XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, value_list);

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME,
				XCB_ATOM_STRING, 8, strlen("jpte"), "jpte");

	xcb_map_window(conn, window);

	xcb_flush(conn);

	gc = xcb_generate_id(conn);
	uint32_t value_mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND;
	uint32_t value_list_gc[] = {screen->white_pixel, screen->black_pixel};
	xcb_create_gc(conn, gc, window, value_mask, value_list_gc);

	// TODO: do I need to setup anything here?
	//font = xcb_generate_id(conn);
	//xcb_open_font(conn, font, strlen("fixed"), "fixed");
	//xcb_change_gc(conn, gc, XCB_GC_FONT, &font);

	keysyms = xcb_key_symbols_alloc(conn);
	if (!keysyms) {
		die("setup_xcb: unable to allocate key symbols");
	}
	debug_print("setup_xcb: done\n");
}

/* Initializes TSM (Terminal State Machine) */
static void setup_tsm(void)
{
	if (tsm_screen_new(&tsm_screen, NULL, NULL) != 0) {
		die("setup_tsm: failed to create TSM screen");
	}
	if (tsm_vte_new(&vte, tsm_screen, write_cb, NULL, NULL, NULL) != 0) {
		die("setup_tsm: failed to create TSM VTE");
	}
	debug_print("setup_tsm: done\n");
}

/* Spawns the shell in a pseudo-terminal */
static void spawn_shell(void)
{
	/* forkpty() combines openpty(), fork(2) and loginpty() */
	pid_t pid = forkpty(&ptty_fd, NULL, NULL, NULL);
	if (pid == -1) {
		die("spawn_shell: forkpty:");
	}
	if (pid == 0) {
		if (execlp(JPTE_SHELL, JPTE_SHELL, NULL) != 0) {
			die("spawn_shell: execlp:");
		}
	}
	debug_print("spawn_shell: done\n");
}

/* Frees resources before exiting */
static void cleanup(void)
{
	xcb_key_symbols_free(keysyms);
	xcb_disconnect(conn);
	tsm_vte_unref(vte);
	tsm_screen_unref(tsm_screen);
	free_glyphs();
	debug_print("cleanup: done\n");
}

int main(void)
{
	// TODO: getopt, sigaction

	setup_stbtt();
	setup_tsm();
	setup_xcb();
	spawn_shell();
	event_loop();
	cleanup();
	return 0;
	//TODO: return retval;
}

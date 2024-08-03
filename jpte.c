#include <xcb/xproto.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <pty.h>
#include <libtsm.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include "config.h"

int ptty_fd;
struct tsm_vte *vte;
struct tsm_screen *tsm_screen;
xcb_connection_t *conn;
xcb_screen_t *screen;
xcb_window_t window;
xcb_gcontext_t gc;
xcb_font_t font;
xcb_key_symbols_t *keysyms;

#ifdef DEBUG
#define debug_print(...)	fprintf(stderr, __VA_ARGS__);
#else
#define debug_print(...)	;
#endif

/* Render a single glyph */
/* void render_glyph(stbtt_fontinfo *font, int size, unsigned char *bitmap, int *width, int *height) */
/* { */
/* 	int x, y; */
/* 	//TODO: Render the first glyph in the font??? What does the next line do idk */
/* 	bitmap = stbtt_GetCodepointBitmap(font, 0, size, 'A', width, height, &x, &y); */
/* } */

/* Load font data */
/* void load_font(const char *font_path, stbtt_fontinfo *font) */
/* { */
/* 	FILE *file = fopen(font_path, "rb"); */
/* 	if (!file) { */
/* 		perror("fopen"); */
/* 		exit(EXIT_FAILURE); */
/* 	} */
/* 	fseek(file, 0, SEEK_END); */
/* 	long font_size = ftell(file); */
/* 	fseek(file, 0, SEEK_SET); */
/* 	unsigned char *font_buffer = (unsigned char *)malloc(font_size); //TODO: I will need to free this later right??? */
/* 	fread(font_buffer, 1, font_size, file); */
/* 	fclose(file); */
/* 	stbtt_InitFont(font, font_buffer, stbtt_GetFontOffsetForIndex(font_buffer, 0)); */
/* } */

/* Writes data to the master file descriptor and prints debug information */
static void write_cb(struct tsm_vte *vte, const char *u8, size_t len, void *data)
{
	(void)vte;
	(void)data;
	write(ptty_fd, u8, len);
	debug_print("write_cb: %.*s\n", (int)len, u8);
}

/* Feeds terminal output into the VTE */
void handle_output(const char *buffer, size_t length)
{
	tsm_vte_input(vte, buffer, length);
	debug_print("handle_output: %.*s\n", (int)length, buffer);
}

/* Draws text on the XCB window */
void draw_text(xcb_drawable_t drawable, xcb_gcontext_t gc, int16_t x, int16_t y,
		const char *text)
{
	xcb_image_text_8(conn, strlen(text), drawable, gc, x, y, text);
	xcb_flush(conn);
	debug_print("draw_text: '%s' at (%d, %d)\n", text, x, y);
}

/* Callback function for drawing characters */
int draw_cb(struct tsm_screen *con,
		uint64_t id,
		const uint32_t *ch,
		size_t len,
		unsigned int width,
		unsigned int posx,
		unsigned int posy,
		const struct tsm_screen_attr *attr,
		tsm_age_t age,
		void *data)
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
	//debug_print("draw_cb: tsm_tcs4_to_utf8(%d, %s): result: %d\n", *ch, buf, bytes);
	buf[bytes] = '\0';
	if (bytes > 0 && buf[0] != '\0') {
		draw_text(window, gc, posx * 10, posy * 20, buf);
	}
	return 0;
}

/* Clears the screen and redraws it using `tsm_screen_draw` */
void draw_screen()
{
	xcb_clear_area(conn,
			0,	/* exposures */
			window,	/* window id */
			0, 0,	/* x, y */
			screen->width_in_pixels,
			screen->height_in_pixels);

	tsm_screen_draw(tsm_screen,	/* connection */
			draw_cb,	/* draw context buffer */
			NULL);		/* data */

	xcb_flush(conn);
	debug_print("draw_screen: done\n");
}

/* Handles key press events, sends input to the VTE and the shell */
void handle_key_press(xcb_key_press_event_t *kp)
{
	char buf[32];
	xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, kp->detail, 0);
	if (keysym) {
		int len = snprintf(buf, sizeof(buf), "%c", keysym);
		buf[len] = '\0';
		tsm_vte_handle_keyboard(vte, buf[0], 0, 0, 0);
		write(ptty_fd, buf, len);
		debug_print("handle_key_press: %s\n", buf);
	}
}

/* Processes XCB events and handles terminal output */
void event_loop()
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
				default:
					break;
				}
			}
			free(event);
		}
		if (FD_ISSET(ptty_fd, &fds)) {
			n = read(ptty_fd, buf, sizeof(buf));
			debug_print("read: %d bytes: buf '%s'\n", n, buf);
			if (n <= 0) break;
			handle_output(buf, n);
			draw_screen();
		}
	}
}

/* Initializes XCB and creates a window */
void setup_xcb()
{
	conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(conn)) {
		fprintf(stderr, "error: unable to open X display\n");
		exit(EXIT_FAILURE);
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
	uint32_t value_list_gc[] = {screen->black_pixel, screen->white_pixel};
	xcb_create_gc(conn, gc, window, value_mask, value_list_gc);

	font = xcb_generate_id(conn);
	xcb_open_font(conn, font, strlen("fixed"), "fixed");
	xcb_change_gc(conn, gc, XCB_GC_FONT, &font);

	keysyms = xcb_key_symbols_alloc(conn);
	if (!keysyms) {
		fprintf(stderr, "error: unable to allocate key symbols\n");
		exit(EXIT_FAILURE);
	}
	debug_print("setup_xcb: done\n");
}

/* Initializes TSM (Terminal State Machine) */
void setup_tsm()
{
	if (tsm_screen_new(&tsm_screen, NULL, NULL) != 0) {
		fprintf(stderr, "error: failed to create TSM screen\n");
		exit(EXIT_FAILURE);
	}
	if (tsm_vte_new(&vte, tsm_screen, write_cb, NULL, NULL, NULL) != 0) {
		fprintf(stderr, "error: failed to create TSM VTE\n");
		exit(EXIT_FAILURE);
	}
	debug_print("setup_tsm: done\n");
}

/* Spawns the shell in a pseudo-terminal */
void spawn_shell()
{
	pid_t pid = forkpty(&ptty_fd, NULL, NULL, NULL);
	if (pid == -1) {
		perror("forkpty");
		exit(EXIT_FAILURE);
	}
	if (pid == 0) {
		if (execlp(JPTE_SHELL, JPTE_SHELL, NULL) != 0) {
			perror("execlp");
			exit(EXIT_FAILURE);
		}
	}
	// TODO: waitpid here?
	debug_print("spawn_shell: done\n");
}

/* Frees resources before exiting */
void cleanup(void)
{
	xcb_key_symbols_free(keysyms);
	xcb_disconnect(conn);
	tsm_vte_unref(vte);
	tsm_screen_unref(tsm_screen);
	debug_print("cleanup: done\n");
}

int main(void)
{
	setup_tsm();
	setup_xcb();
	spawn_shell();
	event_loop();
	cleanup();
	return 0;
}

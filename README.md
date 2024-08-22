# jpte

jpte is a fast and minimal terminal emulator written in C99.

## Dependencies

- **XCB**: X11 C Bindings, client library to communicate with X server.
    - [XCB on freedesktop](https://xcb.freedesktop.org)
    - MIT license
- **libtsm**: A lightweight terminal-emulator state machine library
    - [libtsm on freedesktop](https://www.freedesktop.org/wiki/Software/libtsm/)
    - GNU LGPL 2.1
- **stb_truetype.h**: A single-file TrueType font rendering library.
    - [stb_truetype.h on GitHub](https://github.com/nothings/stb)
    - Public domain

## Install
```sh
$ sudo make clean install
```

## License

MIT License. See `LICENSE` for details.

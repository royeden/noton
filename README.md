# Noton

A minimal logic gates playground, written in ANSI C.

## Build

To build [noton](https://wiki.xxiivv.com/noton), you must have [SDL2](https://wiki.libsdl.org/) and [PortMidi](http://portmedia.sourceforge.net/portmidi/).

```
cc noton.c -std=c89 -Os -DNDEBUG -g0 -s -Wall -L/usr/local/lib -lSDL2 -lportmidi -o noton
```

## Controls

### General

- `BACKSPACE` Erase
- `SPACE` Toggle play
- `1-9` Select channel
- `<` Octave down(TODO)
- `>` Octave up(TODO)

### Paint

- `mouse1` Stroke
- `mouse1+mouse2` Gate

### TODO

- Don't change polarity twice per frame.
- export image
- add change octave shortcut

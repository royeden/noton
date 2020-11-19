# Noton

A minimal logic gates playground, written in ANSI C.

## Build

To build [noton](https://wiki.xxiivv.com/noton), you must have [SDL2](https://wiki.libsdl.org/) and [PortMidi](http://portmedia.sourceforge.net/portmidi/).

## Controls

### General

- `BACKSPACE` Erase last stroke

### Paint

- `mouse1` Stroke
- `mouse1+mouse2` Gate

### TODO

- Don't change polarity twice per frame.
- Implement erase
- Consider changing output to 1 note of 8 bits instead of 12 notes.
- flag deleted nodes and wires
- rename gate value for note?
- export image
- colorize piano notes

## Notes

- Wires and Gates are added to the scene with an active flag.
- When these are destroyed, their id is going to recycled when a new wire or cable is needed.

# Noton

A minimal [color computing playground](https://wiki.xxiivv.com/site/noton.html), written in ANSI C.

Right-click to add nodes, and left-click to add wires. Nodes will emit a positive signal if all the connected wires have the same polarity. The screen has timers to the left, and notes to the right. The default timers are 6 trackers, 4 sequencers and 2 pools.

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
- `up` Octave up
- `down` Octave down
- `right` Speed up
- `left` Speed down

### Paint

- `mouse1` Stroke
- `mouse1+mouse2` Gate

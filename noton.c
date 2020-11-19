#include <SDL2/SDL.h>
#include <portmidi.h>
#include <porttime.h>
#include <stdio.h>

#define HOR 32
#define VER 16
#define PAD 8
#define ZOOM 2
#define color1 0xeeeeee
#define color2 0x000000
#define color3 0xcccccc
#define color4 0x72dec2
#define color0 0xffb545

#define GATEMAX 64
#define WIREMAX 128
#define PORTMAX 8

#define INPUTLEN 8
#define OUTPUTLEN 12

#define CABLEMAX 128
#define SPEED 40
#define OCTAVE 36

typedef enum {
	INPUT,
	OUTPUT,
	XOR
} GateType;

typedef struct {
	int x, y;
} Point2d;

typedef struct Wire {
	int id, active, polarity, a, b, len;
	Point2d points[CABLEMAX];
} Wire;

typedef struct Gate {
	int id, active, polarity, value, locked, inlen, outlen;
	Point2d pos;
	GateType type;
	Wire *inputs[PORTMAX], *outputs[PORTMAX];
} Gate;

typedef struct Arena {
	int frame;
	Gate gates[GATEMAX];
	Wire wires[WIREMAX];
	Gate *inputs[INPUTLEN], *outputs[OUTPUTLEN];
} Arena;

typedef struct Brush {
	int down;
	Point2d pos;
	Wire wire;
} Brush;

int WIDTH = 8 * HOR + PAD * 2;
int HEIGHT = 8 * VER + PAD * 2;
SDL_Window* gWindow = NULL;
SDL_Renderer* gRenderer = NULL;
SDL_Texture* gTexture = NULL;
uint32_t* pixels;
Arena arena;
PmStream* midi;

/* helpers */

int
distance(Point2d a, Point2d b)
{
	return (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
}

Point2d*
setpt2d(Point2d* p, int x, int y)
{
	p->x = x;
	p->y = y;
	return p;
}

Point2d
Pt2d(int x, int y)
{
	Point2d pos;
	setpt2d(&pos, x, y);
	return pos;
}

void
playnote(int val, int z)
{
	Pm_WriteShort(midi,
	              Pt_Time(),
	              Pm_Message(0x90, OCTAVE + val, z ? 100 : 0));
}

Gate*
findgateid(Arena* a, int id)
{
	if(id < 0 || id >= GATEMAX)
		return NULL;
	if(!&a->gates[id] || !a->gates[id].active)
		return NULL;
	return &a->gates[id];
}

Gate*
availgate(Arena* a)
{
	int i;
	for(i = 0; i < GATEMAX; ++i)
		if(!a->gates[i].active) {
			a->gates[i].id = i;
			return &a->gates[i];
		}
	return NULL;
}

Wire*
availwire(Arena* a)
{
	int i;
	for(i = 0; i < WIREMAX; ++i)
		if(!a->wires[i].active) {
			a->wires[i].id = i;
			return &a->wires[i];
		}
	return NULL;
}

Gate*
findgateat(Arena* a, Point2d pos)
{
	int i;
	for(i = 0; i < GATEMAX; ++i) {
		Gate* g = &a->gates[i];
		if(g->active && distance(pos, g->pos) < 50)
			return g;
	}
	return NULL;
}

int
getpolarity(Gate* g)
{
	int i;
	if(!g->active || g->inlen < 1)
		return -1;
	/* TODO: Ignore inactive wires */
	if(g->inlen == 1)
		return g->inputs[0]->polarity;
	for(i = 0; i < g->inlen; i++)
		if(g->inputs[i]->polarity != g->inputs[0]->polarity)
			return 0;
	return 1;
}

void
polarize(Gate* g)
{
	int i;
	if(!g->active)
		return;
	if(g->type == OUTPUT) {
		int newpolarity = getpolarity(g);
		if(newpolarity != -1 && g->polarity != newpolarity)
			playnote(g->value, newpolarity);
		g->polarity = newpolarity;
	} else if(g->type)
		g->polarity = getpolarity(g);
	for(i = 0; i < g->outlen; ++i)
		g->outputs[i]->polarity = g->polarity;
}

void
bang(Gate* g, int depth)
{
	int i, a = depth - 1;
	if(a < 1 || !g || !g->active)
		return;
	polarize(g);
	for(i = 0; i < g->outlen; ++i)
		bang(findgateid(&arena, g->outputs[i]->b), a);
}

/* Add/Remove */

Wire*
addwire(Arena* a, Wire* temp, Gate* from, Gate* to)
{
	int i;
	Wire* w = availwire(a);
	printf("Added wire #%d(#%d->#%d) \n", w->id, from->id, to->id);
	w->active = 1;
	w->polarity = -1;
	w->a = from->id;
	w->b = to->id;
	w->len = 0;
	for(i = 0; i < temp->len; i++)
		setpt2d(&w->points[w->len++], temp->points[i].x, temp->points[i].y);
	return w;
}

Gate*
addgate(Arena* a, GateType type, int polarity, Point2d pos)
{
	Gate* g = availgate(a);
	printf("Added gate #%d \n", g->id);
	g->active = 1;
	g->polarity = polarity;
	g->value = 0;
	g->inlen = 0;
	g->outlen = 0;
	g->type = type;
	g->pos = pos;
	return g;
}

/* Wiring */

void
extendwire(Brush* b)
{
	if(b->wire.len >= CABLEMAX)
		return;
	if(b->wire.len > 0 && distance(b->wire.points[b->wire.len - 1], b->pos) < 20)
		return;
	setpt2d(&b->wire.points[b->wire.len++], b->pos.x, b->pos.y);
}

void
beginwire(Brush* b)
{
	Gate* gate = findgateat(&arena, b->pos);
	b->wire.active = 1;
	if(gate) {
		b->wire.polarity = gate->polarity;
		setpt2d(&b->pos, gate->pos.x, gate->pos.y);
	}
	b->wire.len = 0;
	extendwire(b);
}

int
abandon(Brush* b)
{
	b->wire.len = 0;
	b->wire.active = 0;
	return 0;
}

int
endwire(Brush* b)
{
	Wire* newwire;
	Gate *gatefrom, *gateto;
	if(b->wire.len < 1)
		return abandon(b);
	gatefrom = findgateat(&arena, b->wire.points[0]);
	if(!gatefrom)
		return abandon(b);
	gateto = findgateat(&arena, b->pos);
	if(!gateto)
		return abandon(b);
	if(!gateto->type)
		return abandon(b);
	if(gatefrom == gateto)
		return abandon(b);
	if(gatefrom->outlen >= PORTMAX)
		return abandon(b);
	if(gateto->inlen >= PORTMAX)
		return abandon(b);
	setpt2d(&b->pos, gateto->pos.x, gateto->pos.y);
	extendwire(b);
	/* copy */
	newwire = addwire(&arena, &b->wire, gatefrom, gateto);
	/* connect */
	gatefrom->outputs[gatefrom->outlen++] = newwire;
	gateto->inputs[gateto->inlen++] = newwire;
	polarize(gateto);
	return abandon(b);
}

/* draw */

void
pixel(uint32_t* dst, int x, int y, int color)
{
	if(x >= 0 && x < WIDTH - PAD * 2 && y >= 0 && y < HEIGHT - PAD * 2)
		dst[(y + PAD) * WIDTH + (x + PAD)] = color;
}

void
line(uint32_t* dst, int ax, int ay, int bx, int by, int color)
{
	int dx = abs(bx - ax), sx = ax < bx ? 1 : -1;
	int dy = -abs(by - ay), sy = ay < by ? 1 : -1;
	int err = dx + dy, e2;
	for(;;) {
		pixel(dst, ax, ay, color);
		if(ax == bx && ay == by)
			break;
		e2 = 2 * err;
		if(e2 >= dy) {
			err += dy;
			ax += sx;
		}
		if(e2 <= dx) {
			err += dx;
			ay += sy;
		}
	}
}

void
circle(uint32_t* dst, int ax, int ay, int d, int color)
{
	int i, r = d / 2;
	for(i = 0; i < d * d; ++i) {
		int x = i % d, y = i / d;
		if(distance(Pt2d(ax, ay), Pt2d(ax - r + x, ay - r + y)) < d)
			pixel(dst, ax - r + x, ay - r + y, color);
	}
}

void
drawwire(uint32_t* dst, Wire* w, int color)
{
	int i;
	if(!w->active)
		return;
	for(i = 0; i < w->len - 1; i++) {
		Point2d p1 = w->points[i];
		Point2d* p2 = &w->points[i + 1];
		if(p2) {
			line(dst, p1.x, p1.y, p2->x, p2->y, color);
			if((arena.frame / 3) % w->len != i)
				line(dst, p1.x, p1.y, p2->x, p2->y, w->polarity == 1 ? color4 : !w->polarity ? color0 : color3);
		}
	}
}

void
drawgate(uint32_t* dst, Gate* g)
{
	int r = 17;
	if(!g->active)
		return;
	circle(dst, g->pos.x, g->pos.y, r, g->polarity == 1 ? color4 : g->polarity == 0 ? color0 : color3);
	if(g->type == OUTPUT) {
		pixel(dst, g->pos.x - 1, g->pos.y, color1);
		pixel(dst, g->pos.x + 1, g->pos.y, color1);
		pixel(dst, g->pos.x, g->pos.y - 1, color1);
		pixel(dst, g->pos.x, g->pos.y + 1, color1);
	} else
		pixel(dst, g->pos.x, g->pos.y, color1);
}

void
clear(uint32_t* dst)
{
	int i, j;
	for(i = 0; i < HEIGHT; i++)
		for(j = 0; j < WIDTH; j++)
			dst[i * WIDTH + j] = color1;
}

void
redraw(uint32_t* dst, Brush* b)
{
	int i;
	clear(dst);
	for(i = 0; i < GATEMAX; i++)
		drawgate(dst, &arena.gates[i]);
	for(i = 0; i < WIREMAX; i++)
		drawwire(dst, &arena.wires[i], color2);
	if(b->wire.active)
		drawwire(dst, &b->wire, color3);
	SDL_UpdateTexture(gTexture, NULL, dst, WIDTH * sizeof(uint32_t));
	SDL_RenderClear(gRenderer);
	SDL_RenderCopy(gRenderer, gTexture, NULL, NULL);
	SDL_RenderPresent(gRenderer);
}

/* operation */

void
run(void)
{
	int i;
	arena.inputs[0]->polarity = (arena.frame / 4) % 2;
	arena.inputs[2]->polarity = (arena.frame / 8) % 2;
	arena.inputs[4]->polarity = (arena.frame / 16) % 2;
	arena.inputs[6]->polarity = (arena.frame / 32) % 2;
	arena.inputs[1]->polarity = (arena.frame / 8) % 4 == 0;
	arena.inputs[3]->polarity = (arena.frame / 8) % 4 == 1;
	arena.inputs[5]->polarity = (arena.frame / 8) % 4 == 2;
	arena.inputs[7]->polarity = (arena.frame / 8) % 4 == 3;
	for(i = 0; i < GATEMAX; ++i)
		bang(&arena.gates[i], 10);
	arena.frame++;
}

void
setup(void)
{
	int i;
	Gate *gtrue, *gfalse;
	for(i = 0; i < INPUTLEN; ++i) {
		arena.inputs[i] = addgate(&arena, INPUT, 0, Pt2d(i % 2 == 0 ? 26 : 20, 30 + i * 6));
		arena.inputs[i]->locked = 1;
	}
	for(i = 0; i < OUTPUTLEN; ++i) {
		arena.outputs[i] = addgate(&arena, OUTPUT, 0, Pt2d(WIDTH - (i % 2 == 0 ? 46 : 40), 30 + i * 6));
		arena.outputs[i]->locked = 1;
		arena.outputs[i]->value = i;
	}
	gfalse = addgate(&arena, INPUT, 0, Pt2d((10 % 2 == 0 ? 26 : 20), 30 + 10 * 6));
	gfalse->locked = 1;
	gtrue = addgate(&arena, INPUT, 1, Pt2d((11 % 2 == 0 ? 26 : 20), 30 + 11 * 6));
	gtrue->locked = 1;
}

/* options */

int
error(char* msg, const char* err)
{
	printf("Error %s: %s\n", msg, err);
	return 0;
}

void
quit(void)
{
	free(pixels);
	Pm_Terminate();
	SDL_DestroyTexture(gTexture);
	gTexture = NULL;
	SDL_DestroyRenderer(gRenderer);
	gRenderer = NULL;
	SDL_DestroyWindow(gWindow);
	gWindow = NULL;
	SDL_Quit();
	exit(0);
}

void
domouse(SDL_Event* event, Brush* b)
{
	switch(event->type) {
	case SDL_MOUSEBUTTONDOWN:
		setpt2d(&b->pos,
		        (event->motion.x - (PAD * ZOOM)) / ZOOM,
		        (event->motion.y - (PAD * ZOOM)) / ZOOM);
		if(event->button.button == SDL_BUTTON_RIGHT)
			break;
		b->down = 1;
		beginwire(b);
		redraw(pixels, b);
		break;
	case SDL_MOUSEMOTION:
		if(event->button.button == SDL_BUTTON_RIGHT)
			break;
		if(b->down) {
			setpt2d(&b->pos,
			        (event->motion.x - (PAD * ZOOM)) / ZOOM,
			        (event->motion.y - (PAD * ZOOM)) / ZOOM);
			extendwire(b);
			redraw(pixels, b);
		}
		break;
	case SDL_MOUSEBUTTONUP:
		setpt2d(&b->pos,
		        (event->motion.x - (PAD * ZOOM)) / ZOOM,
		        (event->motion.y - (PAD * ZOOM)) / ZOOM);
		if(event->button.button == SDL_BUTTON_RIGHT) {
			if(!findgateat(&arena, b->pos))
				addgate(&arena, XOR, -1, b->pos);
			redraw(pixels, b);
			break;
		}
		b->down = 0;
		endwire(b);
		redraw(pixels, b);
		break;
	}
}

void
dokey(SDL_Event* event, Brush* b)
{
	/* int shift = SDL_GetModState() & KMOD_LSHIFT || SDL_GetModState() & KMOD_RSHIFT; */
	switch(event->key.keysym.sym) {
	case SDLK_ESCAPE:
		quit();
		break;
	case SDLK_n:
		/* TODO */
		redraw(pixels, b);
		break;
	case SDLK_BACKSPACE:
		/* TODO */
		redraw(pixels, b);
		break;
	}
}

void
initmidi(void)
{
	int i, select = 0;
	Pm_Initialize();
	for(i = 0; i < Pm_CountDevices(); ++i)
		printf("#%d -> %s%s\n", i,
		       Pm_GetDeviceInfo(i)->name, i == select ? "(selected)" : "");
	Pm_OpenOutput(&midi, select, NULL, 128, 0, NULL, 1);
}

int
init(void)
{
	if(SDL_Init(SDL_INIT_VIDEO) < 0)
		return error("Init", SDL_GetError());
	printf("init\n");
	gWindow = SDL_CreateWindow("Noton",
	                           SDL_WINDOWPOS_UNDEFINED,
	                           SDL_WINDOWPOS_UNDEFINED,
	                           WIDTH * ZOOM,
	                           HEIGHT * ZOOM,
	                           SDL_WINDOW_SHOWN);
	if(gWindow == NULL)
		return error("Window", SDL_GetError());
	gRenderer = SDL_CreateRenderer(gWindow, -1, 0);
	if(gRenderer == NULL)
		return error("Renderer", SDL_GetError());
	gTexture = SDL_CreateTexture(gRenderer,
	                             SDL_PIXELFORMAT_ARGB8888,
	                             SDL_TEXTUREACCESS_STATIC,
	                             WIDTH,
	                             HEIGHT);
	if(gTexture == NULL)
		return error("Texture", SDL_GetError());
	pixels = (uint32_t*)malloc(WIDTH * HEIGHT * sizeof(uint32_t));
	if(pixels == NULL)
		return error("Pixels", "Failed to allocate memory");
	clear(pixels);
	initmidi();
	return 1;
}

int
main(int argc, char** argv)
{
	Uint32 begintime = 0;
	Uint32 endtime = 0;
	Uint32 delta = 0;
	short fps = 60;

	Brush brush;
	brush.down = 0;
	brush.wire.active = 0;

	if(!init())
		return error("Init", "Failure");

	setup();

	while(1) {
		SDL_Event event;
		if(!begintime)
			begintime = SDL_GetTicks();
		else
			delta = endtime - begintime;

		if(delta < SPEED)
			SDL_Delay(SPEED - delta);
		if(delta > SPEED)
			fps = 1000 / delta;
		if(fps < 15)
			printf("Slowdown: %ifps\n", fps);

		run();
		redraw(pixels, &brush);

		while(SDL_PollEvent(&event) != 0) {
			if(event.type == SDL_QUIT)
				quit();
			else if(event.type == SDL_MOUSEBUTTONUP ||
			        event.type == SDL_MOUSEBUTTONDOWN ||
			        event.type == SDL_MOUSEMOTION) {
				domouse(&event, &brush);
			} else if(event.type == SDL_KEYDOWN)
				dokey(&event, &brush);
			else if(event.type == SDL_WINDOWEVENT)
				if(event.window.event == SDL_WINDOWEVENT_EXPOSED)
					redraw(pixels, &brush);
		}

		begintime = endtime;
		endtime = SDL_GetTicks();
	}
	quit();
	(void)argc;
	(void)argv;
	return 0;
}

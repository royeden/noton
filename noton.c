#include <SDL2/SDL.h>
#include <portmidi.h>
#include "porttime.h"
#include <stdio.h>

#define TIME_PROC ((int32_t(*)(void*))Pt_Time)

#define HOR 32
#define VER 16
#define PAD 8
#define ZOOM 2
#define color1 0xeeeeee
#define color2 0x000000
#define color3 0xcccccc
#define color4 0x72dec2
#define color0 0xffb545

#define CABLEMAX 128
#define ROUTEMAX 8
#define SPEED 40

/* TODO 

flag deleted nodes and wires
rename wires to wires
rename gate value for note?
export image
colorize piano notes
gate position should use point2d
*/

typedef enum {
	INPUT,
	OUTPUT,
	NOR
} GateType;

typedef struct {
	int x, y;
} Point2d;

typedef struct Wire {
	int id, active, polarity, a, b, len;
	Point2d points[CABLEMAX];
} Wire;

typedef struct Gate {
	int id, active, polarity, value, inlen, outlen;
	Point2d pos;
	GateType type;
	Wire *inputs[ROUTEMAX], *outputs[ROUTEMAX];
} Gate;

typedef struct Arena {
	int gates_len, wires_len, frame;
	Gate gates[64];
	Wire wires[64];
	Gate *inputs[8], *outputs[36];
} Arena;

typedef struct Brush {
	int x, y;
	int down;
	Wire wire;
} Brush;

int WIDTH = 8 * HOR + PAD * 2;
int HEIGHT = 8 * VER + PAD * 2;
int FPS = 30;
int GUIDES = 0;
SDL_Window* gWindow = NULL;
SDL_Renderer* gRenderer = NULL;
SDL_Texture* gTexture = NULL;
uint32_t* pixels;
Arena arena;
PmStream* midi;

Uint32 begintime = 0;
Uint32 endtime = 0;
Uint32 delta = 0;
short fps = 60;

/* helpers */

int
distance(int ax, int ay, int bx, int by)
{
	return (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
}

Gate*
findgate(int x, int y)
{
	int i;
	for(i = 0; i < arena.gates_len; ++i)
		if(distance(x, y, arena.gates[i].pos.x, arena.gates[i].pos.y) < 50)
			return &arena.gates[i];
	return NULL;
}

Gate*
findgateid(Arena* a, int id)
{
	return id >= 0 ? &a->gates[id] : NULL;
}

int
getpolarity(Gate* g)
{
	int i;
	if(g->inlen < 1)
		return -1;
	if(g->inlen == 1)
		return g->inputs[0]->polarity;
	for(i = 0; i < g->inlen; i++)
		if(g->inputs[i]->polarity != g->inputs[0]->polarity)
			return 0;
	return 1;
}

void
playnote(int val, int z)
{
	if(z)
		Pm_WriteShort(midi, TIME_PROC(NULL), Pm_Message(0x90, 36 + val, 100));
	else
		Pm_WriteShort(midi, TIME_PROC(NULL), Pm_Message(0x90, 36 + val, 0));
}

void
polarize(Gate* g)
{
	int i;
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
	if(a < 1 || !g)
		return;
	polarize(g);
	for(i = 0; i < g->outlen; ++i)
		bang(findgateid(&arena, g->outputs[i]->b), a);
}

void
destroywire(Wire* c)
{
	Gate* ga = findgateid(&arena, c->a);
	Gate* gb = findgateid(&arena, c->b);
	if(ga) {
		printf("Remove wire connection from %d\n", ga->id);
	}
	if(gb) {
		printf("Remove wire connection from %d\n", gb->id);
	}
	c->a = -1;
	c->b = -1;
	c->len = 0;
}

void
destroy(void)
{
	int i;
	if(arena.gates_len <= 22)
		return;
	arena.gates[arena.gates_len - 1].inlen = 0;
	arena.gates[arena.gates_len - 1].outlen = 0;
	for(i = 0; i < arena.wires_len; i++) {
		if(arena.wires[i].a == arena.gates_len - 1)
			destroywire(&arena.wires[i]);
		if(arena.wires[i].b == arena.gates_len - 1)
			destroywire(&arena.wires[i]);
	}
	arena.gates_len--;
}

/* Cabling */

void
append(Wire* c, Brush* b)
{
	if(c->len >= CABLEMAX)
		return;
	if(c->len == 0 || (c->len > 0 && distance(c->points[c->len - 1].x, c->points[c->len - 1].y, b->x, b->y) > 20)) {
		c->points[c->len].x = b->x;
		c->points[c->len].y = b->y;
		c->len++;
	}
}

void
begin(Wire* c, Brush* b)
{
	Gate* gate = findgate(b->x, b->y);
	if(gate) {
		b->wire.polarity = gate->polarity;
		b->x = gate->pos.x;
		b->y = gate->pos.y;
	}
	c->len = 0;
	append(c, b);
}

int
abandon(Wire* c)
{
	c->len = 0;
	return 0;
}

int
terminate(Wire* c, Brush* b)
{
	int i;
	Wire* newwire;
	Gate *gatefrom, *gateto;
	if(c->len < 1)
		return abandon(c);
	gatefrom = findgate(c->points[0].x, c->points[0].y);
	if(!gatefrom)
		return abandon(c);
	gateto = findgate(b->x, b->y);
	if(!gateto)
		return abandon(c);
	if(!gateto->type)
		return abandon(c);
	if(gatefrom == gateto)
		return abandon(c);
	if(gatefrom->outlen >= ROUTEMAX)
		return abandon(c);
	if(gateto->inlen >= ROUTEMAX)
		return abandon(c);
	b->x = gateto->pos.x;
	b->y = gateto->pos.y;
	append(c, b);
	/* copy */
	newwire = &arena.wires[arena.wires_len];
	newwire->id = arena.wires_len;
	newwire->a = gatefrom->id;
	newwire->b = gateto->id;
	for(i = 0; i < c->len; i++) {
		Point2d* bp = &c->points[i];
		Point2d* cp = &newwire->points[i];
		cp->x = bp->x;
		cp->y = bp->y;
		newwire->len++;
	}
	/* connect */
	gatefrom->outputs[gatefrom->outlen++] = newwire;
	gateto->inputs[gateto->inlen++] = newwire;
	polarize(gateto);
	arena.wires_len++;
	return abandon(c);
}

Gate*
addgate(Arena* a, GateType type, int polarity, int x, int y)
{
	a->gates[a->gates_len].pos.x = x;
	a->gates[a->gates_len].pos.y = y;
	a->gates[a->gates_len].id = a->gates_len;
	a->gates[a->gates_len].type = type;
	a->gates[a->gates_len].polarity = polarity;
	return &a->gates[a->gates_len++];
}

/* draw */

void
pixel(uint32_t* dst, int x, int y, int color)
{
	int key = (y + PAD) * WIDTH + (x + PAD);
	if(key >= 0 && key < HEIGHT * WIDTH)
		dst[key] = color;
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
circle(uint32_t* dst, int ax, int ay, int r, int color)
{
	int i;
	for(i = 0; i < r * r; ++i) {
		int x = i % r, y = i / r;
		int dist = distance(ax, ay, ax - r / 2 + x, ay - r / 2 + y);
		if(dist < r)
			pixel(dst, ax - r / 2 + x, ay - r / 2 + y, color);
	}
}

void
update(void)
{
	SDL_SetWindowTitle(gWindow, "Noton");
}

void
drawwire(uint32_t* dst, Wire* c, int color)
{
	int i;
	for(i = 0; i < c->len - 1; i++) {
		Point2d p1 = c->points[i];
		Point2d* p2 = &c->points[i + 1];
		if(p2) {
			line(dst, p1.x, p1.y, p2->x, p2->y, color);
			if((arena.frame / 3) % c->len != i)
				line(dst, p1.x, p1.y, p2->x, p2->y, c->polarity == 1 ? color4 : !c->polarity ? color0 : color3);
		}
	}
}

void
drawgate(uint32_t* dst, Gate* g)
{
	int r = 17;
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
clear(void)
{
	int i, j;
	for(i = 0; i < HEIGHT; i++)
		for(j = 0; j < WIDTH; j++)
			pixels[i * WIDTH + j] = color1;
}

void
redraw(uint32_t* dst, Brush* b)
{
	int i;
	clear();
	for(i = 0; i < arena.gates_len; i++)
		drawgate(dst, &arena.gates[i]);
	for(i = 0; i < arena.wires_len; i++)
		drawwire(dst, &arena.wires[i], color2);
	drawwire(dst, &b->wire, color3);
	SDL_UpdateTexture(gTexture, NULL, dst, WIDTH * sizeof(uint32_t));
	SDL_RenderClear(gRenderer);
	SDL_RenderCopy(gRenderer, gTexture, NULL, NULL);
	SDL_RenderPresent(gRenderer);
}

void
run(uint32_t* dst, Brush* b)
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
	for(i = 0; i < arena.gates_len; ++i)
		bang(&arena.gates[i], 10);
	redraw(dst, b);
	arena.frame++;
}

void
setup(void)
{
	int i;
	for(i = 0; i < 8; ++i) {
		int x = (i % 2 == 0 ? 26 : 20);
		int y = 30 + i * 6;
		arena.inputs[i] = addgate(&arena, INPUT, 0, x, y);
	}
	for(i = 0; i < 12; ++i) {
		int x = WIDTH - (i % 2 == 0 ? 46 : 40);
		int y = 30 + i * 6;
		arena.outputs[i] = addgate(&arena, OUTPUT, 0, x, y);
		arena.outputs[i]->value = i;
	}
	addgate(&arena, INPUT, 0, (10 % 2 == 0 ? 26 : 20), 30 + 10 * 6);
	addgate(&arena, INPUT, 1, (11 % 2 == 0 ? 26 : 20), 30 + 11 * 6);
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
		b->x = (event->motion.x - (PAD * ZOOM)) / ZOOM;
		b->y = (event->motion.y - (PAD * ZOOM)) / ZOOM;
		if(event->button.button == SDL_BUTTON_RIGHT)
			break;
		b->down = 1;
		begin(&b->wire, b);
		redraw(pixels, b);
		break;
	case SDL_MOUSEMOTION:
		if(event->button.button == SDL_BUTTON_RIGHT)
			break;
		if(b->down) {
			b->x = (event->motion.x - (PAD * ZOOM)) / ZOOM;
			b->y = (event->motion.y - (PAD * ZOOM)) / ZOOM;
			append(&b->wire, b);
			redraw(pixels, b);
		}
		break;
	case SDL_MOUSEBUTTONUP:
		b->x = (event->motion.x - (PAD * ZOOM)) / ZOOM;
		b->y = (event->motion.y - (PAD * ZOOM)) / ZOOM;
		if(event->button.button == SDL_BUTTON_RIGHT) {
			addgate(&arena, NOR, -1, b->x, b->y);
			redraw(pixels, b);
			break;
		}
		b->down = 0;
		terminate(&b->wire, b);
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
	case SDLK_h:
		GUIDES = !GUIDES;
		break;
	case SDLK_n:
		arena.gates_len = 22;
		arena.wires_len = 0;
		redraw(pixels, b);
		break;
	case SDLK_BACKSPACE:
		destroy();
		redraw(pixels, b);
		break;
	}
	update();
}

void
echomidi(void)
{
	int i, num = Pm_CountDevices();
	for(i = 0; i < num; ++i) {
		PmDeviceInfo const* info = Pm_GetDeviceInfo(i);
		puts(info->name);
	}
}

void
initmidi(void)
{
	Pm_Initialize();
	echomidi();
	Pm_OpenOutput(&midi, 0, NULL, 128, 0, NULL, 1);
	printf("Midi Output opened.\n");
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
	clear();
	initmidi();

	return 1;
}

int
main(int argc, char** argv)
{
	Brush brush;
	brush.down = 0;

	setup();

	if(!init())
		return error("Init", "Failure");

	update();

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

		run(pixels, &brush);

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

#include <SDL2/SDL.h>
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

#define SZ (HOR * VER * 16)

typedef struct {
	int x, y;
} Point2d;

typedef struct Gate {
	int type, x, y;
} Gate;

typedef struct Cable {
	Point2d points[256];
	Gate *a, *b;
	int len;
} Cable;

typedef struct Arena {
	Gate gates[64];
	int gates_len;
	Cable cables[64];
	int cables_len;
} Arena;

typedef struct Brush {
	int x, y;
	int mode;
	int size;
	int color;
	int down;
	int edit;
	int erase;
	Cable cable;
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
		if(distance(x, y, arena.gates[i].x, arena.gates[i].y) < 20)
			return &arena.gates[i];
	return NULL;
}

/* Cabling */

void
append(Cable* c, Brush* b)
{
	if(!b->cable.a)
		return;
	b->cable.b = findgate(b->x, b->y);
	if(b->cable.a == b->cable.b)
		b->cable.b = NULL;
	if(c->len == 0 || (c->len > 0 && distance(c->points[c->len - 1].x, c->points[c->len - 1].y, b->x, b->y) > 10)) {
		c->points[c->len].x = b->x;
		c->points[c->len].y = b->y;
		c->len++;
	}
}

void
begin(Cable* c, Brush* b)
{
	b->cable.a = findgate(b->x, b->y);
	if(!b->cable.a)
		return;
	b->x = b->cable.a->x;
	b->y = b->cable.a->y;
	c->len = 0;
	append(c, b);
}

void
terminate(Cable* c, Brush* b)
{
	int i;
	if(!b->cable.a)
		return;
	b->cable.b = findgate(b->x, b->y);
	if(!b->cable.b) {
		b->cable.len = 0;
		b->cable.a = NULL;
		return;
	}
	b->x = b->cable.b->x;
	b->y = b->cable.b->y;
	append(c, b);
	for(i = 0; i < c->len; i++) {
		Point2d* bp = &c->points[i];
		Point2d* cp = &arena.cables[arena.cables_len].points[i];
		cp->x = bp->x;
		cp->y = bp->y;
		arena.cables[arena.cables_len].len++;
	}
	arena.cables[arena.cables_len].a = c->a;
	arena.cables[arena.cables_len].b = c->b;
	arena.cables_len++;
	c->len = 0;
}

void
addgate(Arena* a, int x, int y)
{
	a->gates[a->gates_len].x = x;
	a->gates[a->gates_len].y = y;
	a->gates_len++;
}

/* draw */

void
pixel(uint32_t* dst, int x, int y, int color)
{
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
update(void)
{
	SDL_SetWindowTitle(gWindow, "Noton");
}

void
drawcable(uint32_t* dst, Cable* c, int color)
{
	int i, d;
	for(i = 0; i < c->len - 1; i++) {
		Point2d p1 = c->points[i];
		Point2d* p2 = &c->points[i + 1];
		if(p2) {
			line(dst, p1.x, p1.y, p2->x, p2->y, color);
			if(c->a)
				if(distance(c->a->x, c->a->y, p1.x, p1.y) < 200)
					line(dst, p1.x, p1.y, p2->x, p2->y, color4);
			if(c->b)
				if(distance(c->b->x, c->b->y, p2->x, p2->y) < 200)
					line(dst, p1.x, p1.y, p2->x, p2->y, color0);
		}
	}
}

void
drawgate(uint32_t* dst, Gate* g)
{
	int r = 2;
	line(dst, g->x - r, g->y, g->x, g->y - r, color2);
	line(dst, g->x, g->y - r, g->x + r, g->y, color2);
	line(dst, g->x + r, g->y, g->x, g->y + r, color2);
	line(dst, g->x, g->y + r, g->x - r, g->y, color2);
}

void
redraw(uint32_t* dst, Brush* b)
{
	int i;
	for(i = 0; i < arena.cables_len; i++)
		drawcable(dst, &arena.cables[i], color2);
	drawcable(dst, &b->cable, color3);
	for(i = 0; i < arena.gates_len; i++)
		drawgate(dst, &arena.gates[i]);

	SDL_UpdateTexture(gTexture, NULL, dst, WIDTH * sizeof(uint32_t));
	SDL_RenderClear(gRenderer);
	SDL_RenderCopy(gRenderer, gTexture, NULL, NULL);
	SDL_RenderPresent(gRenderer);
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
clear(void)
{
	int i, j;
	for(i = 0; i < HEIGHT; i++)
		for(j = 0; j < WIDTH; j++)
			pixels[i * WIDTH + j] = color1;
}

void
domouse(SDL_Event* event, Brush* b)
{
	switch(event->type) {
	case SDL_MOUSEBUTTONDOWN:
		b->down = 1;
		b->x = (event->motion.x - (PAD * ZOOM)) / ZOOM;
		b->y = (event->motion.y - (PAD * ZOOM)) / ZOOM;
		begin(&b->cable, b);
		redraw(pixels, b);
		break;
	case SDL_MOUSEMOTION:
		if(b->down) {
			b->x = (event->motion.x - (PAD * ZOOM)) / ZOOM;
			b->y = (event->motion.y - (PAD * ZOOM)) / ZOOM;
			append(&b->cable, b);
			redraw(pixels, b);
		}
		break;
	case SDL_MOUSEBUTTONUP:
		b->down = 0;
		b->x = (event->motion.x - (PAD * ZOOM)) / ZOOM;
		b->y = (event->motion.y - (PAD * ZOOM)) / ZOOM;
		terminate(&b->cable, b);
		clear();
		redraw(pixels, b);
		break;
	}
}

void
dokey(SDL_Event* event, Brush* b)
{
	switch(event->key.keysym.sym) {
	case SDLK_ESCAPE:
		quit();
		break;
	case SDLK_TAB:
		b->color = b->color > 2 ? 0 : b->color + 1;
		break;
	case SDLK_h:
		GUIDES = !GUIDES;
		break;
	case SDLK_1:
		b->mode = 0;
		break;
	case SDLK_2:
		b->mode = 1;
		break;
	case SDLK_3:
		b->mode = 2;
		break;
	case SDLK_4:
		b->mode = 3;
		break;
	}
	update();
}

int
init(void)
{
	if(SDL_Init(SDL_INIT_VIDEO) < 0)
		return error("Init", SDL_GetError());
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
	return 1;
}

int
main(int argc, char** argv)
{
	int ticknext = 0;

	Brush brush;
	brush.down = 0;
	brush.color = 1;
	brush.edit = 0;
	brush.size = 10;
	brush.mode = 0;

	addgate(&arena, 40, 40);
	addgate(&arena, 140, 70);
	addgate(&arena, 100, 120);

	if(!init())
		return error("Init", "Failure");

	update();

	while(1) {
		int tick = SDL_GetTicks();
		SDL_Event event;
		if(tick < ticknext)
			SDL_Delay(ticknext - tick);
		ticknext = tick + (1000 / FPS);
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
	}
	quit();
	return 0;
}

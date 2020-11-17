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
#define color0 0xff0000 /* 0xffb545 */

#define SZ (HOR * VER * 16)

typedef struct {
	int x, y;
} Point2d;

typedef struct Cable {
	Point2d points[256];
	int id, a, b, len;
} Cable;

typedef struct Gate {
	int polarity, type, id, x, y;
	Cable *a, *b, *c;
} Gate;

typedef struct Arena {
	Gate gates[64];
	int gates_len;
	Cable cables[64];
	int cables_len;
	Gate* inputs[8];
	int frame;
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
		if(distance(x, y, arena.gates[i].x, arena.gates[i].y) < 50)
			return &arena.gates[i];
	return NULL;
}

/* Cabling */

void
append(Cable* c, Brush* b)
{
	if(c->len == 0 || (c->len > 0 && distance(c->points[c->len - 1].x, c->points[c->len - 1].y, b->x, b->y) > 20)) {
		c->points[c->len].x = b->x;
		c->points[c->len].y = b->y;
		c->len++;
	}
}

void
begin(Cable* c, Brush* b)
{
	Gate* gate = findgate(b->x, b->y);
	b->x = gate ? gate->x : b->x;
	b->y = gate ? gate->y : b->y;
	c->len = 0;
	append(c, b);
}

int
abandon(Cable* c)
{
	c->len = 0;
	return 0;
}

int
terminate(Cable* c, Brush* b)
{
	int i;
	Cable* newcable;
	Gate *gatefrom, *gateto;
	if(c->len < 1)
		return abandon(c);
	gatefrom = findgate(c->points[0].x, c->points[0].y);
	if(!gatefrom)
		return abandon(c);
	gateto = findgate(b->x, b->y);
	if(!gateto)
		return abandon(c);
	if(gateto->a && gateto->b)
		return abandon(c);
	b->x = gateto->x;
	b->y = gateto->y;
	append(c, b);
	/* copy */
	newcable = &arena.cables[arena.cables_len];
	newcable->id = arena.cables_len;
	for(i = 0; i < c->len; i++) {
		Point2d* bp = &c->points[i];
		Point2d* cp = &newcable->points[i];
		cp->x = bp->x;
		cp->y = bp->y;
		newcable->len++;
	}
	gatefrom->c = &arena.cables[arena.cables_len];
	if(!gateto->a)
		gateto->a = &arena.cables[arena.cables_len];
	else
		gateto->b = &arena.cables[arena.cables_len];
	arena.cables_len++;
	return abandon(c);
}

Gate*
addgate(Arena* a, int type, int x, int y)
{
	a->gates[a->gates_len].x = x;
	a->gates[a->gates_len].y = y;
	a->gates[a->gates_len].type = type;
	return &a->gates[a->gates_len++];
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
	printf("Draw cable: %d\n", c->id);
	for(i = 0; i < c->len - 1; i++) {
		Point2d p1 = c->points[i];
		Point2d* p2 = &c->points[i + 1];
		if(p2) {
			line(dst, p1.x, p1.y, p2->x, p2->y, color);
			/*
			if(c->a && (distance(c->a->x, c->a->y, p1.x, p1.y) < 200 || (arena.frame / 2) % c->len != i))
				line(dst, p1.x, p1.y, p2->x, p2->y, c->a->polarity ? color0 : color4);
			*/
		}
	}
}

void
drawgate(uint32_t* dst, Gate* g)
{
	int r = 2;
	if(g->type == 0) {
		line(dst, g->x - r, g->y, g->x, g->y - r, color2);
		line(dst, g->x, g->y - r, g->x + r, g->y, color2);
		line(dst, g->x + r, g->y, g->x, g->y + r, color2);
		line(dst, g->x, g->y + r, g->x - r, g->y, color2);
		pixel(dst, g->x, g->y, g->polarity ? color0 : color4);
		pixel(dst, g->x - 1, g->y, g->polarity ? color0 : color4);
		pixel(dst, g->x, g->y - 1, g->polarity ? color0 : color4);
		pixel(dst, g->x + 1, g->y, g->polarity ? color0 : color4);
		pixel(dst, g->x, g->y + 1, g->polarity ? color0 : color4);
	} else {
		line(dst, g->x - r, g->y - r, g->x + r, g->y - r, color2);
		line(dst, g->x + r, g->y - r, g->x + r, g->y + r, color2);
		line(dst, g->x + r, g->y + r, g->x - r, g->y + r, color2);
		line(dst, g->x - r, g->y + r, g->x - r, g->y - r, color2);
	}
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
	printf("------\n");
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

void
run(uint32_t* dst, Brush* b)
{
	/*
	arena.inputs[0]->polarity = arena.frame % 2;
	arena.inputs[1]->polarity = (arena.frame / 2) % 2;
	arena.inputs[2]->polarity = (arena.frame / 4) % 2;
	arena.inputs[3]->polarity = (arena.frame / 8) % 2;
	arena.inputs[4]->polarity = (arena.frame / 16) % 2;
	arena.inputs[5]->polarity = (arena.frame / 32) % 2;
	arena.inputs[6]->polarity = (arena.frame / 64) % 2;
	arena.inputs[7]->polarity = (arena.frame / 128) % 2;

	redraw(dst, b);
	*/

	arena.frame++;
}

void
setup(void)
{
	int i;
	Gate *on, *off;
	/*
	for(i = 0; i < 8; ++i)
		arena.inputs[i] = addgate(&arena, 0, 10, 20 + i * 10);
	*/
	on = addgate(&arena, 0, 20, 20);
	off = addgate(&arena, 0, 20, 40);
	on->polarity = 1;
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
domouse(SDL_Event* event, Brush* b)
{
	switch(event->type) {
	case SDL_MOUSEBUTTONDOWN:
		b->x = (event->motion.x - (PAD * ZOOM)) / ZOOM;
		b->y = (event->motion.y - (PAD * ZOOM)) / ZOOM;
		if(event->button.button == SDL_BUTTON_RIGHT)
			break;
		b->down = 1;
		begin(&b->cable, b);
		redraw(pixels, b);
		break;
	case SDL_MOUSEMOTION:
		if(event->button.button == SDL_BUTTON_RIGHT)
			break;
		if(b->down) {
			b->x = (event->motion.x - (PAD * ZOOM)) / ZOOM;
			b->y = (event->motion.y - (PAD * ZOOM)) / ZOOM;
			append(&b->cable, b);
			redraw(pixels, b);
		}
		break;
	case SDL_MOUSEBUTTONUP:
		b->x = (event->motion.x - (PAD * ZOOM)) / ZOOM;
		b->y = (event->motion.y - (PAD * ZOOM)) / ZOOM;
		if(event->button.button == SDL_BUTTON_RIGHT) {
			addgate(&arena, 1, b->x, b->y);
			redraw(pixels, b);
			break;
		}
		b->down = 0;
		terminate(&b->cable, b);
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
	int tickrun = 0;

	Brush brush;
	brush.down = 0;
	brush.color = 1;
	brush.edit = 0;
	brush.size = 10;
	brush.mode = 0;

	setup();

	if(!init())
		return error("Init", "Failure");

	update();

	while(1) {
		int tick = SDL_GetTicks();
		SDL_Event event;
		if(tick < ticknext)
			SDL_Delay(ticknext - tick);

		if(tickrun == 1024 * 16) {
			run(pixels, &brush);
			tickrun = 0;
		}
		tickrun++;

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

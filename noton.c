#include <SDL2/SDL.h>
#include <stdio.h>

#define HOR 32
#define VER 16
#define PAD 8
#define ZOOM 2
#define color1 0xeeeeee
#define color2 0x000000
#define color3 0xcccccc
#define color4 0x444444
#define color0 0x111111

#define SZ (HOR * VER * 16)

typedef struct {
	int x, y;
} Point2d;

typedef struct Cable {
	Point2d points[256];
	int len;
} Cable;

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
Cable cables[64];
int cables_len;

/* helpers */

int
distance(int ax, int ay, int bx, int by)
{
	return (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
}

/* Cabling */

void
append(Cable* c, int x, int y)
{
	c->points[c->len].x = x;
	c->points[c->len].y = y;
	c->len++;
}

void
begin(Cable* c, int x, int y)
{
	c->len = 0;
	append(c, x, y);
}

void
terminate(Cable* c, int x, int y)
{
	/* move cable to cables */
	int i;
	append(c, x, y);
	for(i = 0; i < c->len; i++) {
		Point2d* bp = &c->points[i];
		Point2d* cp = &cables[cables_len].points[i];
		cp->x = bp->x;
		cp->y = bp->y;
		cables[cables_len].len++;
	}
	cables_len++;
	c->len = 0;
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
draw(uint32_t* dst)
{

	SDL_UpdateTexture(gTexture, NULL, dst, WIDTH * sizeof(uint32_t));
	SDL_RenderClear(gRenderer);
	SDL_RenderCopy(gRenderer, gTexture, NULL, NULL);
	SDL_RenderPresent(gRenderer);
}

void
update(void)
{
	SDL_SetWindowTitle(gWindow, "Noton");
}

void
drawcable(uint32_t* dst, Cable* c, int color)
{
	int i;
	for(i = 0; i < c->len - 1; i++) {
		Point2d p1 = c->points[i];
		Point2d* p2 = &c->points[i + 1];
		if(p2)
			line(dst, p1.x, p1.y, p2->x, p2->y, color);
	}
}

void
redraw(uint32_t* dst, Brush* b)
{
	int i;
	for(i = 0; i < cables_len; i++) 
		drawcable(dst, &cables[i], color2);
	drawcable(dst, &b->cable, color3);

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
domouse(SDL_Event* event, Brush* b)
{
	switch(event->type) {
	case SDL_MOUSEBUTTONDOWN:
		b->down = 1;
		b->x = (event->motion.x - (PAD * ZOOM)) / ZOOM;
		b->y = (event->motion.y - (PAD * ZOOM)) / ZOOM;
		begin(&b->cable, b->x, b->y);
		redraw(pixels, b);
		break;
	case SDL_MOUSEMOTION:
		if(b->down) {
			b->x = (event->motion.x - (PAD * ZOOM)) / ZOOM;
			b->y = (event->motion.y - (PAD * ZOOM)) / ZOOM;
			append(&b->cable, b->x, b->y);
			redraw(pixels, b);
		}
		break;
	case SDL_MOUSEBUTTONUP:
		b->down = 0;
		b->x = (event->motion.x - (PAD * ZOOM)) / ZOOM;
		b->y = (event->motion.y - (PAD * ZOOM)) / ZOOM;
		terminate(&b->cable, b->x, b->y);
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
	int i, j;
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
	for(i = 0; i < HEIGHT; i++)
		for(j = 0; j < WIDTH; j++)
			pixels[i * WIDTH + j] = color1;
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

	if(!init())
		return error("Init", "Failure");

	draw(pixels);
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
					draw(pixels);
		}
	}
	quit();
	return 0;
}

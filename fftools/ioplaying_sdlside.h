#ifndef IOPLAYING_SDLSIDE
#define IOPLAYING_SDLSIDE
#include "libavfilter/avfilter.h"
#include "libavfilter/ioplaying_interface.h"
#include<SDL.h>
#include <SDL_thread.h>

typedef struct EventWarpper
{
	EventContent content;
	SDL_mutex *mutex;
}EventWarpper;

void event_warpper_init(EventWarpper *warp);
void submit_event(EventWarpper* warp,SDL_Event eve);
void fill_ioplaying(EventWarpper* warp,AVFilterGraph *graph,AVFrame *frm);
void event_warpper_uninit(EventWarpper *warp);

#endif

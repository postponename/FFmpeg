#ifndef IOPLAYING_INTERFACE
#define IOPLAYING_INTERFACE
#include "avfilter.h"
#include "libavcodec/hashtable.h"
#include <SDL2/SDL.h>

typedef struct EventContent
{
    int mouse_x,mouse_y;
    int mouse_lbutton,mouse_rbutton,mouse_mbutton;
    
    int mod_ctrl,mod_alt,mod_shift;
    FFHashtableContext *key_state_map;
    
}EventContent;

typedef struct IOPlayingContext
{
    const AVClass *class;
    
    char *x_expr, *y_expr;
    char *w_expr, *h_expr; 
    char *t_expr;
    
    EventContent event;
}IOPlayingContext;

#endif

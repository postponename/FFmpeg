#ifndef IOPLAYING_INTERFACE
#define IOPLAYING_INTERFACE
#include "avfilter.h"
#include "libavcodec/hashtable.h"
#include "libavutil/eval.h"
#include "libavutil/bprint.h"
#include "libavutil/mem.h"

typedef struct EventContent
{
    int window_w,window_h;
    int once;
    
    int mouse_x,mouse_y;
    int mouse_lbutton,mouse_rbutton,mouse_mbutton;
    int dbl_click;
    
    int mod_ctrl,mod_alt,mod_shift;
    int (*keyname_mapper)(const char*);
    int (*keycode_checker)(int);
    FFHashtableContext *key_state_map;
    
}EventContent;

typedef struct IOPlayingContext
{
    const AVClass *class;
    
    char *cond_expr,*out_expr;
    int start_number;
    
    AVBPrint expanded_out;
    AVBPrint output_expr_prep;
    
    EventContent event;
    AVDictionary **var_dict;
}IOPlayingContext;

#endif

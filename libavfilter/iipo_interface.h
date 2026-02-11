#ifndef IIPO_INTERFACE_H
#define IIPO_INTERFACE_H
#include "avfilter.h"
#include "libavcodec/hashtable.h"
#include "libavutil/eval.h"
#include "libavutil/bprint.h"
#include "libavutil/mem.h"

typedef struct
{
    int x,y;
    int left,right,middle;
    
}MouseStatus;

typedef struct
{
    int mod_ctrl,mod_alt,mod_shift;
    
    void (*keyname_mapper)(const char* name,int *is_valid,int *code);
    
    void *opaque;
    int (*keystatus_getter)(void *opa,int code);
    
}KeyboardStatus;

typedef struct
{
    int window_w,window_h;
    int once;
    MouseStatus mouse;
    KeyboardStatus keyboard;
    AVDictionary **var_dict;
}IOPlayingGlobal;

typedef struct IOPlayingContext
{
    const AVClass *class;
    
    char *cond_expr,*out_expr;
    int start_number;
    
    AVBPrint expanded_out;
    AVBPrint output_expr_prep;
    
    IOPlayingGlobal global;
    
}IOPlayingContext;

typedef struct IndirectContext
{
    const AVClass *class;
    
    char *cond_expr,*vf_desc;
    int start_number;
    int wait_ioplaying;
    
    AVBPrint expr_prep;
    AVBPrint vf_desc_expand;
    
    IOPlayingGlobal ioplaying_global;
}IndirectContext;

typedef struct PlaycallGlobal
{
    int window_w,window_h;
}PlaycallGlobal;

typedef struct PlaycallContext
{
    const AVClass *class;

    char *cond_expr,*call_expr;
    int start_number;
    int req_ioplaying;
    
    
    PlaycallGlobal global;
}PlaycallContext;
#endif

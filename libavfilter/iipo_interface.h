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
    double audio_volume;
    int is_mute;
    int once;
    
    MouseStatus mouse;
    KeyboardStatus keyboard;
    AVDictionary **var_dict;
}IOPlayingGlobal;

enum PlaycallInst : unsigned
{
    pcinst_nop           = 0b00000000,
    pcinst_pause         = 0b00000001,
    pcinst_seek_abs      = 0b00000010,
    pcinst_seek_rel      = 0b00000100, 
    pcinst_volume_abs    = 0b00001000,
    pcinst_volume_rel    = 0b00010000,
    pcinst_mute          = 0b00100000,
};
enum SeekUnit : unsigned
{                                                                                                                                                                                                                                 
    sunit_second         = 1,
    sunit_frame          = 2,
    sunit_ratio          = 3,
};
enum MuteState : unsigned
{                                                                                                                                                                                                                                 
    mstate_enable        = 1,
    mstate_disable       = 2,
    mstate_switch        = 3,
};
typedef struct PlaycallCommand
{
    enum PlaycallInst inst;
    union
    {
        struct
        {
            double value;
            enum SeekUnit unit;
        }seekpos_abs;
        struct
        {
            double value;
            enum SeekUnit unit;
        }seekpos_rel;
        double volume_abs;
        double volume_rel;
        enum MuteState mute_st;
    };
}PlaycallCommand;

typedef struct PlaycallGlobal
{
    int window_w,window_h;
    double audio_volume;
    int is_mute;
    
    void *opaque;
    void (*playcall_transferer)(void *ctx,AVFilterGraph *graph);
    
}PlaycallGlobal;

typedef struct IOPlayingContext
{
    const AVClass *class;
    
    char *cond_expr,*out_expr;
    int start_number;
    
    AVBPrint expanded_out;
    AVBPrint output_expr_prep;
    
    IOPlayingGlobal global;
    
}IOPlayingContext;

typedef void *(*InvokeInitFunc)(char *stapar,void *log_ctx);
typedef void *(*InvokeUninitFunc)(void **opaque_ptr);
typedef void *(*InvokeVFFunc)(void *opaque,int argc,char **argv,int *status);

typedef struct
{
    const char *path;
    void *lib;
    void *opaque;
    InvokeInitFunc init;
    InvokeVFFunc vf;
    InvokeUninitFunc uninit;
    
    void *log_ctx;
}IndirectInvokeContext;

typedef struct IndirectContext
{
    const AVClass *class;
    
    char *cond_expr,*vf_desc;
    char *invoke_cmd;
    char *invoke_stapar;
    int invoke_provide_log;
    int start_number;
    int wait_ioplaying;
    
    const char *invoke_path,*invoke_param;
    AVBPrint expr_prep;
    AVBPrint desc_cmd_expand;
    
    IndirectInvokeContext *invoke_ctx;
    
    IOPlayingGlobal ioplaying_global;
    PlaycallGlobal playcall_global;
    
}IndirectContext;

typedef struct PlaycallContext
{
    const AVClass *class;

    char *call_expr;
    int start_number;
    int req_ioplaying;

    AVBPrint expr_prep;
    
    PlaycallGlobal global;
    PlaycallCommand cmd;
}PlaycallContext;
#endif

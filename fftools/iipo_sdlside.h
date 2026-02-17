#ifndef IOPLAYING_SDLSIDE
#define IOPLAYING_SDLSIDE
#include "libavfilter/iipo_interface.h"
#include "libavfilter/avfilter.h"
#include<SDL.h>
#include<SDL_thread.h>
typedef struct keystatus_getter_context
{
    SDL_mutex *mutex;
    FFHashtableContext *map;
}keystatus_getter_context;

typedef struct PlaycallMultiCommand
{
    unsigned inst_flags;
    double seekpos_abs;
    enum SeekUnit unit_sa;
    double seekpos_rel;
    enum SeekUnit unit_sr;
    double volume_abs;
    double volume_rel;
    enum MuteState mute_state;
}PlaycallMultiCommand;

typedef struct IIPOWarpper
{
	IOPlayingGlobal ioplaying;
    PlaycallGlobal playcall;
    
    SDL_mutex *mutex;
    keystatus_getter_context key_status;
    AVDictionary *var_dict;
    PlaycallMultiCommand playcall_mcmd;
}IIPOWarpper;

double iipo_get_volume_db_norm(int ivol);
int iipo_get_volume_sdl_origin(double dvol);

void event_warpper_init(IIPOWarpper *warp);
void submit_event(IIPOWarpper* warp,SDL_Event eve);
void submit_size_event(IIPOWarpper* warp,int w,int h);
void submit_audio_volume(IIPOWarpper *warp,int volume);
void submit_is_mute(IIPOWarpper *warp,int is_mute);
void fill_globals(IIPOWarpper* warp,AVFilterGraph *graph,AVFrame *frm);
void transfer_playcall(IIPOWarpper* warp,AVFilterGraph *graph);
void call_playcall_handler(void (*playcall_handler)(void *ctx,PlaycallMultiCommand *cmd),IIPOWarpper *warp,void *ctx);
void event_warpper_uninit(IIPOWarpper *warp);

#endif

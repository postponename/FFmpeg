#include"iipo_sdlside.h"
#include"libavutil/ffmath.h"
static void get_key_from_name(const char* name,int *is_valid,int *code)
{
    int keycode=SDL_GetKeyFromName(name);
    if(keycode==SDLK_UNKNOWN)
    {
        *is_valid=0;
        return;
    }
    *is_valid=1;
    *code=keycode;
}

static int keystatus_getter(void *c0,int code)
{
    keystatus_getter_context *ctx=c0;
    SDL_LockMutex(ctx->mutex);
    unsigned char status=0;
    int res=ff_hashtable_get(ctx->map,&code,&status);
    SDL_UnlockMutex(ctx->mutex);
    if(!res)
        status=0;
    return status;
}
static int from_sdlkeycode_to_ioplaying(SDL_Keycode code)
{
    return code;
}

static void transfer_playcall_warp(void *ctx,AVFilterGraph *graph)
{
    transfer_playcall(ctx,graph);
}

void event_warpper_init(IIPOWarpper *warp)
{
    av_log(NULL, AV_LOG_INFO,"event_warpper_init=warp=%p\n",(void*)warp);
    memset(warp,0,sizeof(IIPOWarpper));
    warp->mutex=SDL_CreateMutex();
    
    warp->key_status.mutex=SDL_CreateMutex();
    ff_hashtable_alloc(&warp->key_status.map,sizeof(int),1,512);
    
    warp->ioplaying.keyboard.keyname_mapper=get_key_from_name;
    warp->ioplaying.keyboard.opaque=&warp->key_status;
    warp->ioplaying.keyboard.keystatus_getter=keystatus_getter;
    warp->var_dict=NULL;//Empty dictionary
    warp->ioplaying.var_dict=&warp->var_dict;
    warp->ioplaying.once=1;    
    
    warp->playcall.opaque=warp;
    warp->playcall.playcall_transferer=transfer_playcall_warp;
}

void event_warpper_uninit(IIPOWarpper *warp)
{
    av_log(NULL, AV_LOG_DEBUG,"event_warpper_uninit=warp=%p\n",(void*)warp);
    ff_hashtable_freep(&warp->key_status.map),warp->key_status.map=NULL;
    av_dict_free(&warp->var_dict);
    SDL_DestroyMutex(warp->mutex),warp->mutex=NULL;
}

static void submit_key(IIPOWarpper *warp,SDL_KeyboardEvent key,unsigned char val)
{
    SDL_Keycode sym=key.keysym.sym;
    KeyboardStatus *kb=&warp->ioplaying.keyboard;
    int keycode=from_sdlkeycode_to_ioplaying(sym);
    SDL_LockMutex(warp->key_status.mutex);
    ff_hashtable_set(warp->key_status.map,&keycode,&val);
    SDL_UnlockMutex(warp->key_status.mutex);
    kb->mod_ctrl=((key.keysym.mod&KMOD_CTRL)!=0);
    kb->mod_alt=((key.keysym.mod&KMOD_ALT)!=0);
    kb->mod_shift=((key.keysym.mod&KMOD_SHIFT)!=0);
}
static void submit_button(MouseStatus *ms,SDL_MouseButtonEvent button,int val)
{
    if(button.button==SDL_BUTTON_LEFT)
        ms->left=val;
    if(button.button==SDL_BUTTON_RIGHT)
        ms->right=val;
    if(button.button==SDL_BUTTON_MIDDLE)
        ms->middle=val;
    ms->x=button.x;
    ms->y=button.y;
}
void submit_size_event(IIPOWarpper* warp,int w,int h)
{
    SDL_LockMutex(warp->mutex);
    warp->ioplaying.window_w=w;
    warp->ioplaying.window_h=h;
    warp->playcall.window_w=w;
    warp->playcall.window_h=h;
    SDL_UnlockMutex(warp->mutex);
}
double iipo_get_volume_db_norm(int ivol)
{
    double db=ivol?(20*log(ivol/(double)SDL_MIX_MAXVOLUME)/log(10)):-1000.0;//SDL音量值转分贝
    double db_norm=(db>-60.0)?(db+60.0)/60.0:0.0;//[-60分贝,0分贝]区间内归一化
    return av_clipd(db_norm,0.0,1.0);
}
int iipo_get_volume_sdl_origin(double dvol)
{
    double db=(fabsl(dvol-0.0)<1e-8)?-1000.0:(dvol*60.0-60.0);
    int ivol=lrint(pow(10.0,db/20.0)*SDL_MIX_MAXVOLUME);
    return av_clip(ivol,0,SDL_MIX_MAXVOLUME);
}
void submit_audio_volume(IIPOWarpper *warp,int volume)
{
    double dvol=iipo_get_volume_db_norm(volume);
    SDL_LockMutex(warp->mutex);
    warp->ioplaying.audio_volume=dvol;
    warp->playcall.audio_volume=dvol;
    SDL_UnlockMutex(warp->mutex);
}
void submit_is_mute(IIPOWarpper *warp,int is_mute)
{
    SDL_LockMutex(warp->mutex);
    warp->ioplaying.is_mute=(int)(is_mute!=0);
    warp->playcall.is_mute=(int)(is_mute!=0);
    SDL_UnlockMutex(warp->mutex);
}
void submit_event(IIPOWarpper* warp,SDL_Event event)
{
//    printf("submit_event=warp=%p:eve.type=%u\n",(void*)warp,event.type);
    SDL_LockMutex(warp->mutex);
    switch(event.type)
    {
    case SDL_KEYDOWN:
    {
        submit_key(warp,event.key,1);
        break;
    }
    case SDL_KEYUP:
    {
        submit_key(warp,event.key,0);
        break;
    }
    case SDL_MOUSEMOTION:
    {
        int x=event.motion.x,y=event.motion.y;
        int lb=event.motion.state&SDL_BUTTON_LMASK;
        int rb=event.motion.state&SDL_BUTTON_RMASK;
        int mb=event.motion.state&SDL_BUTTON_MMASK;
        MouseStatus *ms=&warp->ioplaying.mouse;
        ms->x         = x;
        ms->y         = y;
        ms->left      = (lb!=0);
        ms->right     = (rb!=0);
        ms->middle    = (mb!=0);
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
    {
        submit_button(&warp->ioplaying.mouse,event.button,1);
        break;
    }
    case SDL_MOUSEBUTTONUP:
    {
        submit_button(&warp->ioplaying.mouse,event.button,0);
        break;
    }
    }
    SDL_UnlockMutex(warp->mutex);
}

void fill_globals(IIPOWarpper* warp,AVFilterGraph *graph,AVFrame *frm)
{
    SDL_LockMutex(warp->mutex);
//    printf("fill_globals iterator filter...[%d]\n",graph->nb_filters);
    for(int i=0;i<graph->nb_filters;i++)
    {
        AVFilterContext *fi=graph->filters[i];
//        printf("fill_globals check filter...[%s]\n",fi->filter->name);
        if(strcmp(fi->filter->name,"ioplaying")==0)
        {
            IOPlayingContext *iopctx=fi->priv;
            iopctx->global=warp->ioplaying;
//            printf("fill_globals hit ioplaying=index_in_graph=%d:iopctx=%p\n",i,iopctx);
        }
        if(strcmp(fi->filter->name,"indirect")==0)
        {
            IndirectContext *indctx=fi->priv;
            indctx->ioplaying_global=warp->ioplaying;
            indctx->playcall_global=warp->playcall;
        }
        if(strcmp(fi->filter->name,"playcall")==0)
        {
            PlaycallContext *pcctx=fi->priv;
            pcctx->global=warp->playcall;
        }
    }
    
    if(warp->ioplaying.window_w)
    {
        if(warp->ioplaying.once)
        {
            warp->ioplaying.once=0;
        }
    }
    SDL_UnlockMutex(warp->mutex);
}

static void transfer_one_playcall(IIPOWarpper* warp,PlaycallContext *pcctx)
{
    PlaycallMultiCommand *dst=&warp->playcall_mcmd;
    PlaycallCommand *cmd=&pcctx->cmd;
    dst->inst_flags|=cmd->inst;
    switch(cmd->inst)
    {
    case pcinst_pause:
        break;
    case pcinst_seek_abs:
        dst->seekpos_abs=cmd->seekpos_abs.value;
        dst->unit_sa=cmd->seekpos_abs.unit;
        break;
    case pcinst_seek_rel:
        dst->seekpos_rel=cmd->seekpos_rel.value;
        dst->unit_sr=cmd->seekpos_rel.unit;
        break;
    case pcinst_volume_abs:
        dst->volume_abs=cmd->volume_abs;
        break;
    case pcinst_volume_rel:
        dst->volume_rel=cmd->volume_rel;
        break;
    case pcinst_mute:
        dst->mute_state=cmd->mute_st;
        break;
    default:
        break;
    }
}

void transfer_playcall(IIPOWarpper* warp,AVFilterGraph *graph)
{
    SDL_LockMutex(warp->mutex);
    for(int i=0;i<graph->nb_filters;i++)
    {
        AVFilterContext *fi=graph->filters[i];
        if(strcmp(fi->filter->name,"playcall")==0)
        {
            PlaycallContext *pcctx=fi->priv;
            transfer_one_playcall(warp,pcctx);
        }
    }
    SDL_UnlockMutex(warp->mutex);
}

void call_playcall_handler(void (*playcall_handler)(void *ctx,PlaycallMultiCommand *cmd),IIPOWarpper *warp,void *ctx)
{
    SDL_LockMutex(warp->mutex);
    PlaycallMultiCommand cmd=warp->playcall_mcmd;
    warp->playcall_mcmd.inst_flags=0;
    SDL_UnlockMutex(warp->mutex);
    playcall_handler(ctx,&cmd);
}

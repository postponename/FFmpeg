#include"ioplaying_sdlside.h"

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

void event_warpper_init(EventWarpper *warp)
{
    av_log(NULL, AV_LOG_DEBUG,"event_warpper_init=warp=%p\n",(void*)warp);
    memset(warp,0,sizeof(EventWarpper));
    warp->mutex=SDL_CreateMutex();
    
    warp->key_status.mutex=SDL_CreateMutex();
    ff_hashtable_alloc(&warp->key_status.map,sizeof(int),1,512);
    
    warp->global.keyboard.keyname_mapper=get_key_from_name;
    warp->global.keyboard.opaque=&warp->key_status;
    warp->global.keyboard.keystatus_getter=keystatus_getter;
    warp->var_dict=NULL;//Empty dictionary
    warp->global.var_dict=&warp->var_dict;
    warp->global.once=1;
}

void event_warpper_uninit(EventWarpper *warp)
{
    av_log(NULL, AV_LOG_DEBUG,"event_warpper_uninit=warp=%p\n",(void*)warp);
    ff_hashtable_freep(&warp->key_status.map),warp->key_status.map=NULL;
    av_dict_free(&warp->var_dict);
    SDL_DestroyMutex(warp->mutex),warp->mutex=NULL;
}

static void submit_key(EventWarpper *warp,SDL_KeyboardEvent key,unsigned char val)
{
    SDL_Keycode sym=key.keysym.sym;
    KeyboardStatus *kb=&warp->global.keyboard;
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
void submit_size_event(EventWarpper* warp,int w,int h)
{
    warp->global.window_w=w;
    warp->global.window_h=h;
}
void submit_event(EventWarpper* warp,SDL_Event event)
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
        MouseStatus *ms=&warp->global.mouse;
        ms->x         = x;
        ms->y         = y;
        ms->left      = (lb!=0);
        ms->right     = (rb!=0);
        ms->middle    = (mb!=0);
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
    {
        submit_button(&warp->global.mouse,event.button,1);
        break;
    }
    case SDL_MOUSEBUTTONUP:
    {
        submit_button(&warp->global.mouse,event.button,0);
        break;
    }
    }
    SDL_UnlockMutex(warp->mutex);
}

void fill_ioplaying(EventWarpper* warp,AVFilterGraph *graph,AVFrame *frm)
{
    SDL_LockMutex(warp->mutex);
//    printf("fill_ioplaying iterator filter...[%d]\n",graph->nb_filters);
    for(int i=0;i<graph->nb_filters;i++)
    {
        AVFilterContext *fi=graph->filters[i];
//        printf("fill_ioplaying check filter...[%s]\n",fi->filter->name);
        if(strcmp(fi->filter->name,"ioplaying")==0)
        {
            IOPlayingContext *iopctx=fi->priv;
            iopctx->global=warp->global;
//            printf("fill_ioplaying hit ioplaying=index_in_graph=%d:iopctx=%p\n",i,iopctx);
        }
        if(strcmp(fi->filter->name,"indirect")==0)
        {
            IndirectContext *indctx=fi->priv;
            indctx->ioplaying_global=warp->global;
        }
    }
    
    if(warp->global.window_w)
    {
        if(warp->global.once)
        {
            warp->global.once=0;
        }
    }
    SDL_UnlockMutex(warp->mutex);
}


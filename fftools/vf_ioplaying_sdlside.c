#include"ioplaying_sdlside.h"
void submit_key(EventContent *content,SDL_KeyboardEvent key,unsigned char val);
void submit_button(EventContent *content,SDL_MouseButtonEvent button,int val);

static int get_key_from_name(const char* name)
{
    return SDL_GetKeyFromName(name);
}
static int keycode_checker(int keycode)
{
    return keycode!=SDLK_UNKNOWN;
}
static int from_sdlkeycode_to_ioplaying(SDL_Keycode code)
{
    return code;
}

void event_warpper_init(EventWarpper *warp)
{
    printf("event_warpper_init=warp=%p\n",(void*)warp);
    memset(warp,0,sizeof(EventWarpper));
    ff_hashtable_alloc(&warp->content.key_state_map,sizeof(int),1,256);
    warp->content.keyname_mapper=get_key_from_name;
    warp->content.keycode_checker=keycode_checker;
    warp->var_dict=NULL;//Empty dictionary
    warp->content.once=1;
    warp->mutex=SDL_CreateMutex();
}

void event_warpper_uninit(EventWarpper *warp)
{
    printf("event_warpper_uninit=warp=%p\n",(void*)warp);
    ff_hashtable_freep(&warp->content.key_state_map);
    warp->content.key_state_map=NULL;
    SDL_DestroyMutex(warp->mutex);
    av_dict_free(&warp->var_dict);
    warp->mutex=NULL;
}

void submit_key(EventContent *content,SDL_KeyboardEvent key,unsigned char val)
{
    SDL_Keycode sym=key.keysym.sym;
    int keycode=from_sdlkeycode_to_ioplaying(sym);
    ff_hashtable_set(content->key_state_map,&keycode,&val);
    content->mod_ctrl=((key.keysym.mod&KMOD_CTRL)!=0);
    content->mod_alt=((key.keysym.mod&KMOD_ALT)!=0);
    content->mod_shift=((key.keysym.mod&KMOD_SHIFT)!=0);
}
void submit_button(EventContent *content,SDL_MouseButtonEvent button,int val)
{
    if(button.button==SDL_BUTTON_LEFT)
        content->mouse_lbutton=val;
    if(button.button==SDL_BUTTON_RIGHT)
        content->mouse_rbutton=val;
    if(button.button==SDL_BUTTON_MIDDLE)
        content->mouse_mbutton=val;
    content->mouse_x=button.x;
    content->mouse_x=button.y;
    content->dbl_click=(button.clicks==2);
}
void submit_size_event(EventWarpper* warp,int w,int h)
{
    warp->content.window_w=w;
    warp->content.window_h=h;
}
void submit_event(EventWarpper* warp,SDL_Event event)
{
//    printf("submit_event=warp=%p:eve.type=%u\n",(void*)warp,event.type);
    SDL_LockMutex(warp->mutex);
    switch(event.type)
    {
    case SDL_KEYDOWN:
    {
        submit_key(&warp->content,event.key,1);
        break;
    }
    case SDL_KEYUP:
    {
        submit_key(&warp->content,event.key,0);
        break;
    }
    case SDL_MOUSEMOTION:
    {
        int x=event.motion.x,y=event.motion.y;
        int lb=event.motion.state&SDL_BUTTON_LMASK;
        int rb=event.motion.state&SDL_BUTTON_RMASK;
        int mb=event.motion.state&SDL_BUTTON_MMASK;
        warp->content.mouse_x=x;
        warp->content.mouse_y=y;
        warp->content.mouse_lbutton=(lb!=0);
        warp->content.mouse_rbutton=(rb!=0);
        warp->content.mouse_mbutton=(mb!=0);
        break;
    }
    case SDL_MOUSEBUTTONDOWN:
    {
        submit_button(&warp->content,event.button,1);
        break;
    }
    case SDL_MOUSEBUTTONUP:
    {
        submit_button(&warp->content,event.button,0);
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
            iopctx->event=warp->content;
            iopctx->var_dict=&warp->var_dict;
//            printf("fill_ioplaying hit ioplaying=index_in_graph=%d:iopctx=%p\n",i,iopctx);
        }
    }
    
    if(warp->content.window_w)
    {
        if(warp->content.once)
        {
            warp->content.once=0;
        }
    }
    SDL_UnlockMutex(warp->mutex);
}


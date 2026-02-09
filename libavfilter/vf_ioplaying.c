#include "config_components.h"

#include "libavutil/colorspace.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/bprint.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"
#include "libavutil/detection_bbox.h"
#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "video.h"
#include "textutils.h"
#include "ioplaying_interface.h"
#include <math.h>
#include <fenv.h>

static av_cold int init(AVFilterContext *ctx)
{
	IOPlayingContext *ioctx = ctx->priv;
    EventContent event=ioctx->event;
    av_log(ioctx, AV_LOG_DEBUG, 
           "init=event.key_state_map=%p:event.mouse_x=%d\n",(void*)event.key_state_map,event.mouse_x);
    av_bprint_init(&ioctx->expanded_out,0,AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&ioctx->output_expr_prep,0,AV_BPRINT_SIZE_UNLIMITED);
    
	return 0;
}
static av_cold void uninit(AVFilterContext *ctx)
{
    IOPlayingContext *ioctx = ctx->priv;
    av_bprint_finalize(&ioctx->expanded_out,NULL);
    av_bprint_finalize(&ioctx->output_expr_prep,NULL);
}



static const enum AVPixelFormat pix_fmts[] = {
	AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
	AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
	AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
	AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUVJ440P,
	AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
	AV_PIX_FMT_RGB24,  AV_PIX_FMT_BGR24,
	AV_PIX_FMT_RGBA,   AV_PIX_FMT_BGRA,
	AV_PIX_FMT_ARGB,   AV_PIX_FMT_ABGR,
	AV_PIX_FMT_0RGB,   AV_PIX_FMT_0BGR,
	AV_PIX_FMT_RGB0,   AV_PIX_FMT_BGR0,
	AV_PIX_FMT_NONE
};


static void eval_mods_expr(char *expr,EventContent *event,int *has_mods,int *res)
{
    double res_dbl=0;
    const char *const_names[]=
    {
        "ctrl",
        "shift",
        "alt",
        "mouse_left",
        "mouse_right",
        "mouse_mid",
        NULL
    };
    const double const_values[]=
    {
        (double)event->mod_ctrl,
        (double)event->mod_shift,
        (double)event->mod_alt,
        (double)event->mouse_lbutton,
        (double)event->mouse_rbutton,
        (double)event->mouse_mbutton,
    };
    int expr_len=strlen(expr);
    if (strchr(expr,'~'))
    {
        av_log(NULL, AV_LOG_WARNING, 
               "提示：~符号未支持，如需非运算请使用(1-表达式)，例如~ctrl → (1-ctrl)/4)\n");
    }
    for(int i=0;i<expr_len;i++)
    {
        if(expr[i]=='|')
            expr[i]='+';
        if(expr[i]=='&')
            expr[i]='*';
    }
    int eval_errcode=av_expr_parse_and_eval(&res_dbl,expr,const_names,const_values,NULL,NULL,NULL,NULL,NULL,0,0);
    if(eval_errcode<0)
    {
        *has_mods=0;
        av_log(NULL, AV_LOG_ERROR, 
               "parse mods failed:invalid expr(transformed) '%s'\n",expr);
        return;
    }
    *has_mods=1;
    *res=(int)(res_dbl>0);
}

typedef struct
{
    const char *name;
    int (*eval)(IOPlayingContext *log_ctx,char **argv,int argc,EventContent *event);    
}eval_cond_entry;
static int eval_cond_key(IOPlayingContext *log_ctx,char **argv,int argc,EventContent *event);
static int eval_cond_mouse(IOPlayingContext *log_ctx,char **argv,int argc,EventContent *event);
static int eval_cond_all(IOPlayingContext *log_ctx,char **argv,int argc,EventContent *event);
static int eval_cond_never(IOPlayingContext *log_ctx,char **argv,int argc,EventContent *event);
static int eval_cond_once(IOPlayingContext *log_ctx,char **argv,int argc,EventContent *event);
static const eval_cond_entry eval_cond_table[]=
{
    {"key",      eval_cond_key      },
    {"mouse",    eval_cond_mouse    },
    {"all",      eval_cond_all      },
    {"never",    eval_cond_never    },
    {"once",     eval_cond_once     },
};

static void keycode_from_name(IOPlayingContext *log_ctx,EventContent *event,const char *keyname,int *valid,int *out_keycode)
{
    if(!event->keyname_mapper)
    {
        av_log(NULL, AV_LOG_ERROR, 
               "parse condition for key failed:invalid keyname mapper\n");
        *valid=0;
    }
    int keycode=event->keyname_mapper(keyname);
    if(!event->keycode_checker)
    {
        av_log(log_ctx, AV_LOG_ERROR, 
               "parse condition for key failed:invalid keycode checker\n");
        *valid=0;
    }
    if(!event->keycode_checker(keycode))
    {
        av_log(log_ctx, AV_LOG_ERROR, 
               "parse condition for key failed:invalid key '%s'[%d]\n",keyname,keycode);
        *valid=0;
    }
    *valid=1;
    *out_keycode=keycode;
}

static int get_key_status(EventContent *event,int keycode)
{
    unsigned char keystatus=0;
    int key_query_res=ff_hashtable_get(event->key_state_map,&keycode,&keystatus);
    if(!key_query_res)
        keystatus=0;
    return keystatus;
}

static int eval_cond_key(IOPlayingContext *log_ctx,char **argv,int argc,EventContent *event)
{
    if(argc<2)
    {
        av_log(log_ctx, AV_LOG_ERROR, 
               "The condition type 'key' must have at least TWO parameters,but only %d\n",argc);
    }
    
    int valid_key=0,keycode=0;
    keycode_from_name(log_ctx,event,argv[0],&valid_key,&keycode);
    
    if(!valid_key)
        return 0;
    
    int keystatus=get_key_status(event,keycode);
    
    char *action=argv[1];
    int action_int=-1;
    if(strcmp(action,"release")==0||strcmp(action,"up")==0||strcmp(action,"0")==0)
    {
        action_int=0;
    }
    if(strcmp(action,"press")==0||strcmp(action,"down")==0||strcmp(action,"1")==0)
    {
        action_int=1;
    }
    if(action_int==-1)
    {
        av_log(log_ctx, AV_LOG_ERROR, 
               "parse condition for mouse failed:unknown action '%s'\n",action);
        return 0;
    }
    
    return (keystatus&&action_int)||(!keystatus&&!action_int);
}
static int eval_cond_mouse(IOPlayingContext *log_ctx,char **argv,int argc,EventContent *event)
{
    if(argc<2)
    {
        av_log(log_ctx, AV_LOG_ERROR, 
               "The condition type 'mouse' must have at least TWO parameters,but only %d\n",argc);
    }
    
    char *btnname=argv[0];
    int status=-1;
    if(strcmp(btnname,"left")==0||strcmp(btnname,"l")==0)
        status=event->mouse_lbutton;
    if(strcmp(btnname,"right")==0||strcmp(btnname,"r")==0)
        status=event->mouse_lbutton;
    if(strcmp(btnname,"middle")==0||strcmp(btnname,"m")==0||strcmp(btnname,"mid")==0)
        status=event->mouse_mbutton;
    if(status<0)
    {
        av_log(log_ctx, AV_LOG_ERROR, 
               "parse condition for mouse failed:unknown mouse buton '%s'\n",btnname);
        return 0;
    }
    
    char *action=argv[1];
    
    int action_int=-1;
    if(strcmp(action,"release")==0||strcmp(action,"up")==0||strcmp(action,"0")==0)
    {
        action_int=0;
    }
    if(strcmp(action,"press")==0||strcmp(action,"down")==0||strcmp(action,"1")==0)
    {
        action_int=1;
    }
    if(strcmp(action,"dbl")==0||strcmp(action,"dblclk")==0||strcmp(action,"double_click")==0)
    {
        action_int=2;
    }
    if(action_int==-1)
    {
        av_log(log_ctx, AV_LOG_ERROR, 
               "parse condition for mouse failed:unknown action '%s'\n",action);
        return 0;
    }
    
    if(action_int==2)
        return event->dbl_click;
    return (action_int&&status)||(!action_int&&!status);
}
static int eval_cond_all(IOPlayingContext *log_ctx,char **argv,int argc,EventContent *event)
{
    return 1;
}
static int eval_cond_never(IOPlayingContext *log_ctx,char **argv,int argc,EventContent *)
{
    return 0;
}
static int eval_cond_once(IOPlayingContext *log_ctx,char **argv,int argc,EventContent *event)
{
    return event->once;
}

static int eval_condition_args(IOPlayingContext *log_ctx,EventContent *event,const char *name,char **argv,int argc)
{
    int type=-1;
    int nb_eval_cond=FF_ARRAY_ELEMS(eval_cond_table);
    for(int i=0;i<nb_eval_cond;i++)
    {
        if(strcmp(eval_cond_table[i].name,name)==0)
        {
            type=i;
            break;
        }
    }
    if(type<0)
    {
        av_log(log_ctx, AV_LOG_ERROR, 
               "Unknown cond type '%s'\n",name);
        return 0;
    }
    
    int eval_cond_res=eval_cond_table[type].eval(log_ctx,argv,argc,event);
    
    int has_mods=0,mods_res=0;
    for(int i=0;i<argc;i++)
    {
        const char *after_name=NULL;
        if(av_strstart(argv[i],"mods",&after_name))
        {
            if(*after_name!='=')
            {
                av_log(log_ctx, AV_LOG_ERROR, 
                       "Excepting '=' near '%s'\n",after_name);
                return 0;
            }
            after_name++;
            
            int arg_len=strlen(argv[i]);
            int expr_len=arg_len-5;//mods=
            char *expr=malloc(expr_len+1);
            expr[expr_len]=0;
            memcpy(expr,after_name,expr_len);
            eval_mods_expr(expr,event,&has_mods,&mods_res);
            free(expr);
            break;
        }
    }
    if(has_mods)
        return mods_res&&eval_cond_res;
    return eval_cond_res;
}

static int eval_condition(IOPlayingContext *log_ctx,const char *cond,EventContent *event)
{
    const char *ori_cond=cond;
    char *argv[16];
    int argc=0;
    int res=0;
    while (*cond)
    {
        if(!(argv[argc++]=av_get_token(&cond,":")))
        {
            av_log(log_ctx, AV_LOG_ERROR, 
                   "The condition parse failed near '%s'\n",cond);
            goto end;
        }
        if(argc==FF_ARRAY_ELEMS(argv))
        {
            av_log(log_ctx, AV_LOG_WARNING, 
                   "too much arguments (more than 16) : '%s'\n",ori_cond);
            av_freep(&argv[--argc]);
        }
        cond++;
    }
    res=eval_condition_args(log_ctx,event,argv[0],argv+1,argc-1);
    
end:
    for(int i=0;i<argc;i++)
        av_freep(&argv[i]);
    return res;
}

typedef struct
{
    const char *name;
    void (*expand)(void *ctx,AVBPrint *bp,const char *name,char **argv,int argc);
}output_value_expr_func_entry;
static void out_func_pict_type(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void out_func_pts(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void out_func_frame_num(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void out_func_metadata(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void out_func_strftime(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void out_func_eval_expr(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void out_func_eval_expr_int_fmt(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void out_func_if(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static output_value_expr_func_entry out_func_table[]=
{
    {"pict_type",          out_func_pict_type          },
    {"pts",                out_func_pts                },
    {"frame_num",          out_func_frame_num          },
    {"n",                  out_func_frame_num          },
    {"metadata",           out_func_metadata           },
    {"gmtime",             out_func_strftime           },
    {"localtime",          out_func_strftime           },
    {"expr",               out_func_eval_expr          },
    {"e",                  out_func_eval_expr          },
    {"expr_int_format",    out_func_eval_expr_int_fmt  },
    {"eif",                out_func_eval_expr_int_fmt  },
    {"if",                 out_func_if                 },
};

static const char *const out_value_expr_var_names[]=
{
    "sar",
    "dar",
    "main_h",                 ///< height of the input video
    "main_w",                 ///< width  of the input video
    "scr_h",                  ///< screen height
    "scr_w",                  ///< screen width
    "n",                      ///< number of frame
    "t",                      ///< timestamp expressed in seconds
    "x",                      ///< mouse cursor x coordinate
    "y",                      ///< mouse cursor y coordinate
    "pict_type",
    "duration",
    "mouse_left",
    "mouse_right",
    "mouse_middle",
    "mods_ctrl",
    "mods_shift",
    "mods_alt",
    "double_click",
    "once",
    
    /*var_dict elements will be added dynamically*/
    /*key(KeyName) will be evaluated as unary function calls*/
    
    NULL
};
enum out_value_expr_var_index
{
    VAR_SAR,
    VAR_DAR,
    VAR_MAIN_H,
    VAR_MAIN_W,
    VAR_SCR_H,
    VAR_SCR_W,
    VAR_N,
    VAR_T,
    VAR_X,
    VAR_Y,
    VAR_PICT_TYPE,
    VAR_DURATION,
    VAR_MOUSE_LEFT,
    VAR_MOUSE_RIGHT,
    VAR_MOUSE_MIDDLE,
    VAR_MODS_CTRL,
    VAR_MODS_SHIFT,
    VAR_MODS_ALT,
    VAR_DOUBLE_CLICK,
    VAR_ONCE,
    VAR_NUMBER,
};


typedef struct
{
    AVFilterLink *inlink;
    AVFilterContext *ctx;
    IOPlayingContext *io;
    AVFrame *frm;
    
    output_value_expr_func_entry *func_table;
    
    double pts;
    int frame_number;
    const char **var_names;
    int nb_var_from_dict;//We need to remember it because the variable output can modify the var dict
    double *var_values;
    const char **f1_names;
    double (**f1_ptrs)(void*,double);
    
}output_value_expr_expansion_context;

static double strtod_silent(const char *str)
{
    if(!str||*str=='\0')
    {
        return NAN;
    }
    
    char *endptr=NULL;
    errno=0;
    
    double val=av_strtod(str,&endptr);
    
    if (errno!=0||endptr==str||*endptr!='\0')
    {
        return NAN;
    }
    return val;
}
static void fill_event_vars(double *vars,EventContent *event)
{
    vars[VAR_SCR_H]=event->window_h;
    vars[VAR_SCR_W]=event->window_w;
    vars[VAR_X]=event->mouse_x;
    vars[VAR_Y]=event->mouse_y;
    vars[VAR_MOUSE_LEFT]=event->mouse_lbutton;
    vars[VAR_MOUSE_RIGHT]=event->mouse_rbutton;
    vars[VAR_MOUSE_MIDDLE]=event->mouse_mbutton;
    vars[VAR_MODS_CTRL]=event->mod_ctrl;
    vars[VAR_MODS_SHIFT]=event->mod_shift;
    vars[VAR_MODS_ALT]=event->mod_alt;
    vars[VAR_DOUBLE_CLICK]=event->dbl_click;
    vars[VAR_ONCE]=event->once;
}

static double key_func_warp(void *c0,double keycode)
{
    output_value_expr_expansion_context *ctx=c0;
    
    return get_key_status(&ctx->io->event,keycode);
}

static const char *output_value_expr_f1_names[]=
{
    "key",
    NULL
};
double (*output_value_expr_f1_ptrs[])(void*,double)=
{
    key_func_warp,
    NULL
};

static void fill_expansion_context(output_value_expr_expansion_context *ctx)
{
    AVFilterLink *inlink=ctx->inlink;
    FilterLink *ff_inl=ff_filter_link(ctx->inlink);
    ctx->pts=((ctx->frm->pts==AV_NOPTS_VALUE)?NAN:(ctx->frm->pts*av_q2d(ctx->inlink->time_base)));
    ctx->frame_number=ff_inl->frame_count_out+ctx->io->start_number;
    
    AVDictionary *var_dict=*ctx->io->var_dict;
    ctx->nb_var_from_dict=av_dict_count(var_dict);
    int nb_var=VAR_NUMBER+ctx->nb_var_from_dict;
    void *var_names_ptr=av_malloc(sizeof(char*)*(nb_var+1));
    ctx->var_names=var_names_ptr;
    char **var_names=var_names_ptr;
    ctx->var_values=(double*)av_malloc(sizeof(double)*(nb_var+1));
    double *var_vals=ctx->var_values;
    
    memcpy(var_names,out_value_expr_var_names,sizeof(char*)*VAR_NUMBER);
    memset(var_names+VAR_NUMBER,0,sizeof(char)*ctx->nb_var_from_dict);
    var_names[nb_var]=NULL;
    
    var_vals[VAR_SAR]=inlink->sample_aspect_ratio.num ? av_q2d(inlink->sample_aspect_ratio) : 1;
    var_vals[VAR_DAR]=(double)inlink->w / inlink->h * var_vals[VAR_SAR];
    var_vals[VAR_MAIN_H]=inlink->h;
    var_vals[VAR_MAIN_W]=inlink->w;
    var_vals[VAR_N]=ctx->frame_number;
    var_vals[VAR_T]=ctx->pts;
    var_vals[VAR_PICT_TYPE]=ctx->frm->pict_type;
    var_vals[VAR_DURATION]=ctx->frm->duration*av_q2d(inlink->time_base);
    fill_event_vars(var_vals,&ctx->io->event);
    var_vals[nb_var]=0;

    int i=VAR_NUMBER;
    for(const AVDictionaryEntry *e=av_dict_iterate(var_dict,NULL);e;e=av_dict_iterate(var_dict,e),i++)
    {
        av_assert0(i<nb_var);
        var_names[i]=av_strdup(e->key);
        var_vals[i]=strtod_silent(e->value);
    }
    
    ctx->f1_names=output_value_expr_f1_names;
    ctx->f1_ptrs=output_value_expr_f1_ptrs;
}
static void free_expansion_context(output_value_expr_expansion_context *ctx)
{
    int nb_var=VAR_NUMBER+ctx->nb_var_from_dict;
    for(int i=VAR_NUMBER;i<nb_var;i++)
        av_freep(&ctx->var_names[i]);
    av_freep(&ctx->var_names);
    
    av_freep(&ctx->var_values);
}

static void out_func_pict_type(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    output_value_expr_expansion_context *ctx=c0;
    
    av_bprintf(bp,"%c",av_get_picture_type_char(ctx->frm->pict_type));
}
static void out_func_pts(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    output_value_expr_expansion_context *ctx=c0;
    
    const char *fmt;
    const char *strftime_fmt = NULL;
    const char *delta = NULL;
    double pts = ctx->pts;
    
    // argv: [FMT, [DELTA, 24HH | strftime_fmt]]
    
    fmt=((argc>=1)?argv[0]:"flt");
    if(argc>=2)
    {
        delta=argv[1];
    }
    if(argc>=3)
    {
        if(strcmp(fmt,"hms")==0)
        {
            if(strcmp(argv[2], "24HH")==0)
            {
                av_log(ctx->io,AV_LOG_WARNING,"pts third argument 24HH is deprecated, use pts:hms24hh instead\n");
                fmt="hms24";
            }
            else
            {
                av_log(ctx->io,AV_LOG_ERROR,"Invalid argument '%s', '24HH' was expected\n",argv[2]);
                return;
            }
        }
        else
        {
            strftime_fmt=argv[2];
        }
    }
    
    int res=ff_print_pts(ctx->io, bp, pts, delta, fmt, strftime_fmt);
    if(res<0)
    {
        av_log(ctx->io,AV_LOG_ERROR,"Failed to print pts, funcname=%s,pts=%f,delta=%s,fmt=%s,strftime_fmt=%s\n",name,pts,delta,fmt,strftime_fmt);
    }
}
static void out_func_frame_num(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    output_value_expr_expansion_context *ctx=c0;
    
    av_bprintf(bp,"%d",ctx->frame_number);
}
static void out_func_metadata(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    output_value_expr_expansion_context *ctx=c0;
    char *metakey=argv[0];
    char *defval=NULL;
    if(argc>=2)
        defval=argv[1];
    
    AVDictionaryEntry *e=av_dict_get(ctx->frm->metadata,metakey,NULL,0);
    
    if (e&&e->value)
        av_bprintf(bp,"%s",e->value);
    else if(defval)
        av_bprintf(bp,"%s",defval);
}
static void out_func_strftime(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    output_value_expr_expansion_context *ctx=c0;
    const char *strftime_fmt = argc ? argv[0] : NULL;
    
    int res=ff_print_time(ctx->io,bp,strftime_fmt,strcmp(name,"localtime")==0);
    if(res<0)
    {
        av_log(ctx->io,AV_LOG_ERROR,"Failed to print time, funcname=%s,strftime_fmt=%s\n",name,strftime_fmt);
    }
}

typedef struct
{
    const char *name;
    void (*prep)(output_value_expr_expansion_context *ctx,AVBPrint *bp,const char* name,const char *args);
}expr_prep_func_entry;
static void expr_prep_keyname(output_value_expr_expansion_context *ctx,AVBPrint *bp,const char* name,const char *args);
static void expr_prep_metadata_call(output_value_expr_expansion_context *ctx,AVBPrint *bp,const char* name,const char *args);

static const expr_prep_func_entry prep_func_table[]=
{
    {"key",         expr_prep_keyname          },
    {"metadata",    expr_prep_metadata_call    }
};


static void expr_prep_keyname(output_value_expr_expansion_context *ctx,AVBPrint *bp,const char* func_name,const char *args)
{
    const char *keyname=args;
    
    int valid_key=0,keycode=0;
    keycode_from_name(ctx->io,&(ctx->io->event),keyname,&valid_key,&keycode);
    
    if(!valid_key)
    {
        av_log(ctx->io,AV_LOG_ERROR,"Expr preprocess: Invalid key name '%s' in %s() function\n",keyname,func_name);
        return;
    }
    
    av_bprintf(bp,"%s(%d)",func_name,keycode);
}
static void expr_prep_metadata_call(output_value_expr_expansion_context *ctx,AVBPrint *bp,const char* func_name,const char *args)
{
    char *metakey=av_get_token(&args,",");
    char *defval=NULL;
    if(*args==',')
    {
        args++;
        if(*args)
        {
            defval=av_get_token(&args,"");
        }
    }
    
    AVDictionaryEntry *e=av_dict_get(ctx->frm->metadata,metakey,NULL,0);
    
    if (e&&e->value)
        av_bprintf(bp,"%s",e->value);
    else if(defval)
        av_bprintf(bp,"%s",defval);
    
    av_freep(&metakey);
    av_freep(&defval);
}






static void skip_whitespace(const char **expr)
{
    while (*expr && av_isspace(**expr)) {
        (*expr)++;
    }
}
static int match_do_prep_funcs(output_value_expr_expansion_context *ctx,const char **expr,AVBPrint *bp)
{
    int nb_prep_func=FF_ARRAY_ELEMS(prep_func_table);
    int matched=0;
    
    for(int i=0;i<nb_prep_func;i++)
    {
        const char *func_name=prep_func_table[i].name;
        int func_len=strlen(func_name);
        
        if(strncmp(*expr,func_name,func_len)==0)
        {
            matched=1;
            (*expr)+=func_len;
            
            skip_whitespace(expr); // 先跳过括号前的空白
            if(**expr!='(')
            {
                av_log(ctx->io,AV_LOG_ERROR,"Expr preprocess: Expected '(' after '%s' near '%s'\n",func_name,*expr);
                goto fail;
            }
            (*expr)++; // 跳过左括号
            
            char *args_tk=av_get_token(expr,")");
            if(!args_tk||*args_tk=='\0'||**expr!=')')
            {
                av_log(ctx->io,AV_LOG_ERROR,"Expr preprocess:Unmatched '(' or invalid arguments in %s() function near '%s'\n",func_name,*expr);
                av_freep(&args_tk);
                goto fail;
            }
            (*expr)++;// 跳过右括号
            
            prep_func_table[i].prep(ctx,bp,func_name,args_tk);
            av_freep(&args_tk);
            
            break;
        }
    }
    
    return matched;
fail:
    return -1;
}
static const char* expr_preprocess(output_value_expr_expansion_context *ctx,const char *expr)
{
    AVBPrint *bp=&ctx->io->output_expr_prep;
    av_bprint_clear(bp);
    
    
    while(*expr)
    {
        int matched=match_do_prep_funcs(ctx,&expr,bp);
        if(matched<0)
        {
            goto fail;
        }
        if(!matched)
        {
            av_bprint_chars(bp,*expr,1);
            expr++;
        }
    }
    
    if(!av_bprint_is_complete(bp))
    {
        av_log(ctx->io,AV_LOG_ERROR,"Expr preprocess buffer overflow\n");
        goto fail;
    }
    
    return bp->str;
    
fail:
    av_bprint_clear(bp);
    return "";
}

static double do_eval_expr(output_value_expr_expansion_context *ctx,const char *ori_expr)
{
    const char *expr=expr_preprocess(ctx,ori_expr);
    
    AVExpr *expr_tree=NULL;
    
    int parse_ret=av_expr_parse(&expr_tree,expr,
                                ctx->var_names,
                                ctx->f1_names,ctx->f1_ptrs,
                                NULL,NULL,
                                0, ctx->io);
    if(parse_ret<0)
    {
        av_log(ctx->io, AV_LOG_ERROR,
               "Text expansion expression '%s' is not valid %d\n",
               expr,__LINE__);
    }
    
    double value=av_expr_eval(expr_tree,ctx->var_values,ctx);
    return value;
}

static void out_func_eval_expr(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    output_value_expr_expansion_context *ctx=c0;
    
    double value=do_eval_expr(ctx,argv[0]);
    av_bprintf(bp, "%f", value);
}
static void out_func_eval_expr_int_fmt(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    output_value_expr_expansion_context *ctx=c0;
    
    char *expr=argv[0];
    char format=argv[1][0];
    int positions = -1;
    
    /*
    * argv[0] expression to be converted to `int`
    * argv[1] format: 'x', 'X', 'd' or 'u'
    * argv[2] positions printed (optional)
    */
    
    if(argc==3)
    {
        int ret=sscanf(argv[2],"%u",&positions);
        if(ret!= 1)
        {
            av_log(ctx,AV_LOG_ERROR,"expr_int_format(): Invalid number of positions to print: '%s'\n", argv[2]);
            return;
        }
    }
    
    double value=do_eval_expr(ctx,expr);
    
    if (!strchr("xXdu", format))
    {
        av_log(ctx->io, AV_LOG_ERROR, "Invalid format '%c' specified,"
               " allowed values: 'x', 'X', 'd', 'u'\n", format);
        return;
    }
    
    feclearexcept(FE_ALL_EXCEPT);
    int intval=value;
#if defined(FE_INVALID) && defined(FE_OVERFLOW) && defined(FE_UNDERFLOW)
    int fetestexcept_ret;
    if ((fetestexcept_ret =fetestexcept(FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW)))
    {
        av_log(ctx->io, AV_LOG_ERROR, "Conversion of floating-point result to int failed. Control register: 0x%08x. Conversion result: %d\n", fetestexcept_ret, intval);
        return;
    }
#endif
    char fmt_str[30] = "%";
    
    if (positions >= 0)
        av_strlcatf(fmt_str, sizeof(fmt_str), "0%u", positions);
    av_strlcatf(fmt_str, sizeof(fmt_str), "%c", format);
    
    av_log(ctx->io, AV_LOG_DEBUG, "Formatting value %f (expr '%s') with spec '%s'\n",
           value, expr, fmt_str);
    
    av_bprintf(bp, fmt_str, intval);
}
static void out_func_if(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    output_value_expr_expansion_context *ctx=c0;
    
    char *expr=argv[0];
    char *true_str=argv[1];
    char *false_str=NULL;
    if(argc>=3)
        false_str=argv[2];
    
    double value=do_eval_expr(ctx,expr);
    if(value!=0)
        av_bprintf(bp,"%s",true_str);
    else if(false_str)
        av_bprintf(bp,"%s",false_str);
}


static void do_expand_function(output_value_expr_expansion_context *ctx,AVBPrint *bp,char *name,char **argv,int argc)
{    
    int func=-1;
    int nb_out_func=FF_ARRAY_ELEMS(out_func_table);
    
    for(int i=0;i<nb_out_func;i++)
    {
        if(strcmp(out_func_table[i].name,name)==0)
        {
            func=i;
            break;
        }
    }
    if(func<0)
    {
        av_log(ctx->io, AV_LOG_ERROR, 
               "Unknown out func '%s'\n",name);
        return;
    }
    
    ctx->func_table[func].expand(ctx,bp,name,argv,argc);
}

static void expand_output_value_function(output_value_expr_expansion_context *ctx,AVBPrint *bp,const char **pexpr)
{
    const char *expr=*pexpr;
    if(*expr!='{')
    {
        av_log(ctx->io, AV_LOG_ERROR, "Stray %% near '%s'\n", expr);
        return;
    }
    expr++;
    char *argv[16];
    int argc=0;
    while(1)
    {
        if(!(argv[argc++]=av_get_token(&expr, ":}")))
        {
            av_log(ctx->io,AV_LOG_ERROR,"av_get_token failed because of onmem\n");
            goto end;
        }
        if (!*expr)
        {
            av_log(ctx->io,AV_LOG_ERROR,"Unterminated %%{} near '%s'\n", *pexpr);
            goto end;
        }
        if(argc==FF_ARRAY_ELEMS(argv))
            av_freep(&argv[--argc]);
        if(*expr=='}')
            break;
        expr++;
    }
    
    *pexpr=expr+1;
    do_expand_function(ctx,bp,argv[0],argv+1,argc-1);
    
end:
    for(int i=0;i<argc;i++)
        av_freep(&argv[i]);
}
static char *expand_value_expr(const char *value_expr,output_value_expr_expansion_context *ctx)
{
    AVBPrint *bp=&ctx->io->expanded_out;
    av_bprint_clear(bp);
    if (!value_expr)
        return 0;
    
    while(*value_expr)
    {
        if(*value_expr=='\\'&&value_expr[1])
        {
            av_bprint_chars(bp,value_expr[1],1);
            value_expr+=2;
        }
        else if(*value_expr=='%')
        {
            value_expr++;
            expand_output_value_function(ctx,bp,&value_expr);
        }
        else
        {
            av_bprint_chars(bp,*value_expr,1);
            value_expr++;
        }
    }
    if (!av_bprint_is_complete(bp))
    {
        av_log(ctx->io,AV_LOG_ERROR,"av_get_token failed because of onmem\n");
    }
    
    return bp->str;
}

typedef struct
{
    const char *name;
    void (*execute)(output_value_expr_expansion_context *ctx,char *key,char *val);    
}output_entry;
static void output_to_nop(output_value_expr_expansion_context *ctx,char *,char *);
static void output_to_metadata(output_value_expr_expansion_context *ctx,char *key,char *val);
static void output_to_file(output_value_expr_expansion_context *ctx,char *key,char *val);
static void output_to_print(output_value_expr_expansion_context *ctx,char *,char *val);
static void output_to_variable(output_value_expr_expansion_context *ctx,char *,char *val);
static const output_entry output_table[]=
{
    {"nop",        output_to_nop        },
    {"metadata",   output_to_metadata   },
    {"file",       output_to_file       },
    {"print",      output_to_print      },
    {"var",        output_to_variable   },
};

static int parse_output(IOPlayingContext *log_ctx,const char *out,char **output_key,char **output_value)
{
    char *name=av_get_token(&out,":="); // 分隔符是:或者=，p会自动移动到下一个段
    if(!name||*name =='\0')
    {
        av_log(log_ctx, AV_LOG_ERROR, 
               "The condition parse failed near '%s'\n",out);
        av_freep(&name);
        return 0;
    }

    int type=-1;
    int nb_output=FF_ARRAY_ELEMS(output_table);
    for(int t=0;t<nb_output;t++)
    {
        if(strcmp(output_table[t].name,name)==0)
        {
            type=t;
            break;
        }
    }
    if(type<0)
    {
        av_log(log_ctx, AV_LOG_ERROR, 
               "Unknown out type '%s'\n",name);
        return -1;
    }
    
    char *key=NULL,*value=NULL;
    if(!*out)
    {
        *output_key=key,
        *output_value=value;
        return type;
    }
    if(*out=='=')
    {
        out++;
        value=av_get_token(&out,"");
        *output_key=key,
        *output_value=value;
        return type;
    }
    
    out++;
    
    key=av_get_token(&out,"=");
    if(key&&*out=='=')
    {
        out++;
        value=av_get_token(&out,"");
    }
    
    *output_key=key;
    *output_value=value;
    return type;
}

static void output_to_nop(output_value_expr_expansion_context *,char *,char *)
{
    
}
static void output_to_metadata(output_value_expr_expansion_context *ctx,char *key,char *val)
{
    av_dict_set(&ctx->frm->metadata,key,val,0);
}
static void output_to_file(output_value_expr_expansion_context *ctx,char *key,char *val)
{
    FILE *fp=fopen(key,"w");
    if(!fp)
    {
        av_log(ctx->io,AV_LOG_ERROR,"failed to open file to write:%s\n",key);
        return;
    }
    fwrite(val,strlen(val),1,fp);
    fclose(fp);
}
static void output_to_print(output_value_expr_expansion_context *ctx,char *,char *val)
{
    av_log(ctx->io,AV_LOG_INFO,"%s\n",val);
}
static void output_to_variable(output_value_expr_expansion_context *ctx,char *key,char *val)
{
    av_dict_set(ctx->io->var_dict,key,val,0);
}

static void handle_output(AVFilterLink *inlink,AVFrame *frm,int type,char **output_key,char **output_value)
{
    if(type>=0)
    {
        output_value_expr_expansion_context expansion_ctx=
        {
            .inlink=inlink,
            .ctx=inlink->dst,
            .io=inlink->dst->priv,
            .frm=frm,
            .func_table=out_func_table
        };
        fill_expansion_context(&expansion_ctx);
        char *expanded_value=(*output_value)?expand_value_expr(*output_value,&expansion_ctx):NULL;
        output_table[type].execute(&expansion_ctx,*output_key,expanded_value);
        free_expansion_context(&expansion_ctx);
    }
    av_freep(output_key);
    av_freep(output_value);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
	AVFilterContext *ctx = inlink->dst;
	IOPlayingContext *ioctx=ctx->priv;
    if(!ioctx->event.window_w)
    {
        av_log(ioctx, AV_LOG_DEBUG, 
               "context not initialized,do nothing\n");
        return ff_filter_frame(ctx->outputs[0], frame);
    }
    
    if(eval_condition(ioctx,ioctx->cond_expr,&ioctx->event))
    {
        av_log(ioctx, AV_LOG_DEBUG, 
               "ioplaying triggered cond=%s, out=%s\n",ioctx->cond_expr,ioctx->out_expr);
        char *output_key=NULL,*output_value=NULL;
        int type=parse_output(ioctx,ioctx->out_expr,&output_key,&output_value);
        handle_output(inlink,frame,type,&output_key,&output_value);
    }
    return ff_filter_frame(ctx->outputs[0], frame);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args, char *res, int res_len, int flags)
{
	return 0;
}

#define OFFSET(x) offsetof(IOPlayingContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM


static const AVOption ioplaying_options[]=
{
	{ "cond",            "condition to toggle the output",                       OFFSET(cond_expr),    AV_OPT_TYPE_STRING, { .str="never" },       0, 0,       FLAGS },
	{ "out",             "the output action when the condition is satisfied",    OFFSET(out_expr),     AV_OPT_TYPE_STRING, { .str="nop="  },       0, 0,       FLAGS },
    { "start_number",    "start frame number for n/frame_num variable",          OFFSET(start_number), AV_OPT_TYPE_INT,    { .i64=0       },       0, INT_MAX, FLAGS},
	{ NULL }
};

AVFILTER_DEFINE_CLASS(ioplaying);

static const AVFilterPad ioplaying_inputs[] = 
{
	{.name="default",.type=AVMEDIA_TYPE_VIDEO,.filter_frame=filter_frame},
};

const FFFilter ff_vf_ioplaying = {
	.p.name        = "ioplaying",
	.p.description = NULL_IF_CONFIG_SMALL("Dummy filter for playing interactive action"),
	.p.priv_class  = &ioplaying_class,
	.p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
	.priv_size     = sizeof(IOPlayingContext),
	.init          = init,
    .uninit        = uninit,
	FILTER_INPUTS(ioplaying_inputs),
	FILTER_OUTPUTS(ff_video_default_filterpad),
	FILTER_PIXFMTS_ARRAY(pix_fmts),
	.process_command = process_command,
};


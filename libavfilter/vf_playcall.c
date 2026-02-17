#include "config_components.h"

#include "libavutil/colorspace.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/bprint.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"
#include "libavutil/detection_bbox.h"
#include "libavutil/mem.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"
#include "avfilter.h"
#include "filters.h"
#include "video.h"
#include "iipo_interface.h"
#include "exprpreputil.h"

#include <math.h>
#include <fenv.h>

static av_cold int init(AVFilterContext *ctx)
{
	PlaycallContext *pcctx=ctx->priv;
    pcctx->cmd.inst=pcinst_nop;
    av_bprint_init(&pcctx->expr_prep,0,AV_BPRINT_SIZE_UNLIMITED);
    
	return 0;
}
static av_cold void uninit(AVFilterContext *ctx)
{
    PlaycallContext *pcctx=ctx->priv;
    av_bprint_finalize(&pcctx->expr_prep,NULL);
}

static const enum AVPixelFormat pix_fmts[]=
{
    AV_PIX_FMT_RGB8,           
    AV_PIX_FMT_RGB444,         
    AV_PIX_FMT_RGB555,         
    AV_PIX_FMT_BGR555,         
    AV_PIX_FMT_RGB565,         
    AV_PIX_FMT_BGR565,         
    AV_PIX_FMT_RGB24,          
    AV_PIX_FMT_BGR24,          
    AV_PIX_FMT_0RGB32,         
    AV_PIX_FMT_0BGR32,         
    AV_PIX_FMT_NE(RGB0, 0BGR), 
    AV_PIX_FMT_NE(BGR0, 0RGB), 
    AV_PIX_FMT_RGB32,          
    AV_PIX_FMT_RGB32_1,        
    AV_PIX_FMT_BGR32,          
    AV_PIX_FMT_BGR32_1,        
    AV_PIX_FMT_YUV420P,        
    AV_PIX_FMT_YUYV422,        
    AV_PIX_FMT_UYVY422,        
   
	AV_PIX_FMT_NONE
};

static const char * expr_var_names[]=
{
    "sar",
    "dar",
    "main_h",                 ///< height of the input video
    "main_w",                 ///< width  of the input video
    "n",                      ///< number of frame
    "t",                      ///< timestamp expressed in seconds
    "pict_type",
    "duration",
    "scr_h",
    "scr_w",
    "audio_volume",
    "is_mute",
    NULL
};
static double expr_var_values[]=
{
    0,              //sar
    0,              //dar
    0,              //main_h
    0,              //main_w
    0,              //n
    0,              //t
    0,              //pict_type
    0,              //duration
    0,              //scr_h
    0,              //scr_w
    0,              //audio_volume
    0,              //is_mute
    0               //NULL
};
enum out_value_expr_var_index
{
    VAR_SAR,
    VAR_DAR,
    VAR_MAIN_H,
    VAR_MAIN_W,
    VAR_N,
    VAR_T,
    VAR_PICT_TYPE,
    VAR_DURATION,
    VAR_SCR_H,
    VAR_SCR_W,
    VAR_AUDIO_VOLUME,
    VAR_IS_MUTE,
    VAR_NUMBER,
};

typedef struct
{
    AVFilterLink *inlink;
    AVFilterContext *ctx;
    PlaycallContext *pc;
    AVFrame *frm;
    
    double pts;
    int frame_number;
    const char **var_names;
    double *var_values;
    const char **f1_names;
    double (**f1_ptrs)(void*,double);
    
}playcall_arg_eval_context;


static void fill_eval_context(playcall_arg_eval_context *ctx)
{
    AVFilterLink *inlink=ctx->inlink;
    FilterLink *ff_inl=ff_filter_link(ctx->inlink);
    ctx->pts=((ctx->frm->pts==AV_NOPTS_VALUE)?NAN:(ctx->frm->pts*av_q2d(ctx->inlink->time_base)));
    ctx->frame_number=ff_inl->frame_count_out+ctx->pc->start_number;
    
    ctx->var_names=expr_var_names;
    ctx->var_values=expr_var_values;
    
    ctx->var_values[VAR_SAR]=inlink->sample_aspect_ratio.num ? av_q2d(inlink->sample_aspect_ratio) : 1;
    ctx->var_values[VAR_DAR]=(double)inlink->w / inlink->h * ctx->var_values[VAR_SAR];
    ctx->var_values[VAR_MAIN_H]=inlink->h;
    ctx->var_values[VAR_MAIN_W]=inlink->w;
    ctx->var_values[VAR_N]=ctx->frame_number;
    ctx->var_values[VAR_T]=ctx->pts;
    ctx->var_values[VAR_PICT_TYPE]=ctx->frm->pict_type;
    ctx->var_values[VAR_DURATION]=ctx->frm->duration*av_q2d(inlink->time_base);
    ctx->var_values[VAR_SCR_H]=ctx->pc->global.window_h;
    ctx->var_values[VAR_SCR_W]=ctx->pc->global.window_w;
    ctx->var_values[VAR_AUDIO_VOLUME]=ctx->pc->global.audio_volume;
    ctx->var_values[VAR_IS_MUTE]=ctx->pc->global.is_mute;
    
    ctx->f1_names=NULL;
    ctx->f1_ptrs=NULL;
}
static void free_eval_context(playcall_arg_eval_context *ctx)
{
    
}

static void expr_prep_metadata_call(void *c0,void *log_ctx,AVBPrint *bp,const char* func_name,const char *args)
{
    playcall_arg_eval_context *ctx=c0;
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

static FFExprPreprocer prep_func_table[]=
{
    {"metadata",    expr_prep_metadata_call    }
};

static double do_eval_expr(playcall_arg_eval_context *ctx,const char* ori_expr)
{
    FFExprPrepContext prep_ctx=
    {
        .opaque=ctx,
        .log_ctx=ctx->pc,
        .bp=&ctx->pc->expr_prep,
        .funcs=prep_func_table,
        .nb_funcs=FF_ARRAY_ELEMS(prep_func_table)
    };
    const char *expr=ff_expr_preprocess(&prep_ctx,ori_expr);
    
    AVExpr *expr_tree=NULL;
    
    int parse_ret=av_expr_parse(&expr_tree,expr,
                                ctx->var_names,
                                ctx->f1_names,ctx->f1_ptrs,
                                NULL,NULL,
                                0, ctx->pc);
    if(parse_ret<0)
    {
        av_log(ctx->pc, AV_LOG_ERROR,
               "Text expansion expression '%s' is not valid %d\n",
               expr,__LINE__);
        return NAN;
    }
    
    double value=av_expr_eval(expr_tree,ctx->var_values,ctx);
    return value;
}

typedef struct
{
    const char *name;
    void (*write)(playcall_arg_eval_context *ctx,const char *name,char **argv,int argc);    
}playcall_cmd_entry;
static void playcall_cmd_pause(playcall_arg_eval_context *ctx,const char *name,char **argv,int argc);
static void playcall_cmd_seek(playcall_arg_eval_context *ctx,const char *name,char **argv,int argc);
static void playcall_cmd_volume(playcall_arg_eval_context *ctx,const char *name,char **argv,int argc);
static void playcall_cmd_mute(playcall_arg_eval_context *ctx,const char *name,char **argv,int argc);

static const playcall_cmd_entry playcall_cmd_table[]=
{
    {"pause",      playcall_cmd_pause      },
    {"seek",       playcall_cmd_seek       },
    {"volume",     playcall_cmd_volume     },
    {"mute",       playcall_cmd_mute       }
};

static void playcall_cmd_pause(playcall_arg_eval_context *ctx,const char *name,char **argv,int argc)
{
    ctx->pc->cmd.inst=pcinst_pause;
}
static void playcall_cmd_seek(playcall_arg_eval_context *ctx,const char *name,char **argv,int argc)
{
    if(argc<2)
    {
        av_log(ctx->pc, AV_LOG_ERROR, 
               "The playcall type 'seek' must have at least TWO parameters,but only %d\n",argc);
    }
    double value=do_eval_expr(ctx,argv[0]);
    if(value==NAN)
    {
        ctx->pc->cmd.inst=pcinst_nop;
        return;
    }
    
    char *seek4=argv[1];
    
    char *seek_unit=NULL;
    if(argc>=3)
    {
        seek_unit=argv[2];
    }
    
    enum SeekUnit sunit=sunit_ratio;
    if(seek_unit)
    {
        if(strcmp(seek_unit,"second")==0||strcmp(seek_unit,"sec")==0||strcmp(seek_unit,"s")==0)
        {
            sunit=sunit_second;
        }
        else if(strcmp(seek_unit,"ratio")==0||strcmp(seek_unit,"rat")==0||strcmp(seek_unit,"r")==0)
        {
            sunit=sunit_ratio;
        }
        else if(strcmp(seek_unit,"frame")==0||strcmp(seek_unit,"frm")==0||strcmp(seek_unit,"f")==0)
        {
            sunit=sunit_frame;
        }
        else
        {
            ctx->pc->cmd.inst=pcinst_nop;
            av_log(ctx->pc, AV_LOG_ERROR, 
                   "Unknown seek unit '%s'\n",seek_unit);
        }
    }
    
    if(strcmp(seek4,"absolute")==0||strcmp(seek4,"abs")==0||strcmp(seek4,"a")==0)
    {
        ctx->pc->cmd.inst=pcinst_seek_abs;
        ctx->pc->cmd.seekpos_abs.value=value;
        ctx->pc->cmd.seekpos_abs.unit=sunit;
    }
    else if(strcmp(seek4,"relative")==0||strcmp(seek4,"rel")==0||strcmp(seek4,"r")==0)
    {
        ctx->pc->cmd.inst=pcinst_seek_rel;
        ctx->pc->cmd.seekpos_rel.value=value;
        ctx->pc->cmd.seekpos_rel.unit=sunit;
    }
    else
    {
        ctx->pc->cmd.inst=pcinst_nop;
        av_log(ctx->pc, AV_LOG_ERROR, 
               "Unknown seek type '%s'\n",seek4);
    }
    
}
static void playcall_cmd_volume(playcall_arg_eval_context *ctx,const char *name,char **argv,int argc)
{
    if(argc<2)
    {
        av_log(ctx->pc, AV_LOG_ERROR, 
               "The playcall type 'volume' must have at least TWO parameters,but only %d\n",argc);
    }
    
    double value=do_eval_expr(ctx,argv[0]);
    if(value==NAN)
    {
        ctx->pc->cmd.inst=pcinst_nop;
        return;
    }
    
    char *seek4=argv[1];
    
    if(strcmp(seek4,"absolute")==0||strcmp(seek4,"abs")==0||strcmp(seek4,"a")==0)
    {
        ctx->pc->cmd.inst=pcinst_volume_abs;
        ctx->pc->cmd.volume_abs=value;
    }
    else if(strcmp(seek4,"relative")==0||strcmp(seek4,"rel")==0||strcmp(seek4,"r")==0)
    {
        ctx->pc->cmd.inst=pcinst_volume_rel;
        ctx->pc->cmd.volume_rel=value;
    }
    else
    {
        ctx->pc->cmd.inst=pcinst_nop;
        av_log(ctx->pc, AV_LOG_ERROR, 
               "Unknown volume change type '%s'\n",seek4);
    }
}
static void playcall_cmd_mute(playcall_arg_eval_context *ctx,const char *name,char **argv,int argc)
{
    if(argc<1)
    {
        av_log(ctx->pc, AV_LOG_ERROR, 
               "The playcall type 'mute' must have at least ONE parameters,but only %d\n",argc);
    }
    
    if(strcmp(argv[0],"switch")==0||strcmp(argv[0],"sw")==0)
    {
        ctx->pc->cmd.inst=pcinst_mute;
        ctx->pc->cmd.mute_st=mstate_switch;
        return;
    }
    
    double value=do_eval_expr(ctx,argv[0]);
    if(value==NAN)
    {
        ctx->pc->cmd.inst=pcinst_nop;
        return;
    }
    
    ctx->pc->cmd.inst=pcinst_mute;
    if(value!=0)
        ctx->pc->cmd.mute_st=mstate_enable;
    else
        ctx->pc->cmd.mute_st=mstate_disable;
}

static void do_call_function(playcall_arg_eval_context *ctx,const char *name,char **argv,int argc)
{
    int type=-1;
    int nb_playcall_cmd=FF_ARRAY_ELEMS(playcall_cmd_table);
    for(int i=0;i<nb_playcall_cmd;i++)
    {
        if(strcmp(playcall_cmd_table[i].name,name)==0)
        {
            type=i;
            break;
        }
    }
    if(type<0)
    {
        av_log(ctx->pc, AV_LOG_ERROR, 
               "Unknown cond type '%s'\n",name);
        return;
    }

    playcall_cmd_table[type].write(ctx,name,argv,argc);
}

static void parse_write_call(playcall_arg_eval_context *ctx,const char *call)
{
    const char *ori_call=call;
    char *argv[16];
    int argc=0;
    while (*call)
    {
        if(!(argv[argc++]=av_get_token(&call,":")))
        {
            av_log(ctx->pc, AV_LOG_ERROR, 
                   "The condition parse failed near '%s'\n",call);
            goto end;
        }
        if(argc==FF_ARRAY_ELEMS(argv))
        {
            av_log(ctx->pc, AV_LOG_WARNING, 
                   "too much arguments (more than 16) : '%s'\n",ori_call);
            av_freep(&argv[--argc]);
        }
        if(!*call)
            break;
        call++;
    }
    
    do_call_function(ctx,argv[0],argv+1,argc-1);
end:
    for(int i=0;i<argc;i++)
        av_freep(&argv[i]);
}

static int ioplaying_check(PlaycallContext *pc)
{
    return pc->global.window_w;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
	AVFilterContext *ctx = inlink->dst;
	PlaycallContext *pcctx=ctx->priv;
    if(pcctx->req_ioplaying&&!ioplaying_check(pcctx))
    {
        av_log(pcctx,AV_LOG_DEBUG,"context not initialized,do nothing\n");
        return ff_filter_frame(ctx->outputs[0], frame);
    }
    
    pcctx->cmd.inst=pcinst_nop;
    
    if(!pcctx->call_expr)
    {
        return ff_filter_frame(ctx->outputs[0],frame);
    }
    
    playcall_arg_eval_context eval_ctx=
    {
        .inlink=inlink,
        .ctx=ctx,
        .pc=pcctx,
        .frm=frame
    };
    fill_eval_context(&eval_ctx);
    parse_write_call(&eval_ctx,pcctx->call_expr);
    free_eval_context(&eval_ctx);
    
    return ff_filter_frame(ctx->outputs[0],frame);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args, char *res, int res_len, int flags)
{
	return 0;
}

#define OFFSET(x) offsetof(PlaycallContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM


static const AVOption playcall_options[]=
{
	{ "call",            "set video filters",                                                                                OFFSET(call_expr),         AV_OPT_TYPE_STRING, { .str=NULL    },       0, 0,       FLAGS },
    { "start_number",    "start frame number for n/frame_num variable",                                                      OFFSET(start_number),      AV_OPT_TYPE_INT,    { .i64=0       },       0, INT_MAX, FLAGS },
	{ "req_ioplaying",   "weather the initiation of call depends on the readility of the context for ioplaying ",            OFFSET(req_ioplaying),     AV_OPT_TYPE_INT,    { .i64=1       },       0, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(playcall);

static const AVFilterPad playcall_inputs[] = 
{
	{.name="default",.type=AVMEDIA_TYPE_VIDEO,.filter_frame=filter_frame},
};

const FFFilter ff_vf_playcall = {
	.p.name        = "playcall",
	.p.description = NULL_IF_CONFIG_SMALL("Dummy filter for playcall"),
	.p.priv_class  = &playcall_class,
	.p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
	.priv_size     = sizeof(PlaycallContext),
	.init          = init,
    .uninit        = uninit,
	FILTER_INPUTS(playcall_inputs),
	FILTER_OUTPUTS(ff_video_default_filterpad),
	FILTER_PIXFMTS_ARRAY(pix_fmts),
	.process_command = process_command,
};


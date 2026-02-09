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
#include <math.h>
#include <fenv.h>

typedef struct IndirectContext
{
    const AVClass *class;
    
    char *cond_expr,*vf_desc;
    int start_number;
    
    AVBPrint cond_expr_prep;
}IndirectContext;

static av_cold int init(AVFilterContext *ctx)
{
	IndirectContext *indctx = ctx->priv;
    av_bprint_init(&indctx->cond_expr_prep,0,AV_BPRINT_SIZE_UNLIMITED);
    
	return 0;
}
static av_cold void uninit(AVFilterContext *ctx)
{
    IndirectContext *indctx = ctx->priv;
    av_bprint_finalize(&indctx->cond_expr_prep,NULL);
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

typedef struct
{
    AVFilterLink *inlink;
    AVFilterContext *ctx;
    IndirectContext *ind;
    AVFrame *frm;
    
    const char **var_names;
    double *var_values;
    const char **f1_names;
    double (**f1_ptrs)(void*,double);
    
}cond_expr_evaluation_context;


static void fill_eval_context(cond_expr_evaluation_context *ctx)
{
    ctx->var_names=NULL;
    ctx->var_values=NULL;
    ctx->f1_names=NULL;
    ctx->f1_ptrs=NULL;
}
static void free_eval_context(cond_expr_evaluation_context *ctx)
{
    
}


typedef struct
{
    const char *name;
    void (*prep)(cond_expr_evaluation_context *ctx,AVBPrint *bp,const char* name,const char *args);
}expr_prep_func_entry;
static void expr_prep_metadata_call(cond_expr_evaluation_context *ctx,AVBPrint *bp,const char* name,const char *args);

static const expr_prep_func_entry prep_func_table[]=
{
    {"metadata",    expr_prep_metadata_call    }
};

static void expr_prep_metadata_call(cond_expr_evaluation_context *ctx,AVBPrint *bp,const char* func_name,const char *args)
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
static int match_do_prep_funcs(cond_expr_evaluation_context *ctx,const char **expr,AVBPrint *bp)
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
                av_log(ctx->ind,AV_LOG_ERROR,"Expr preprocess: Expected '(' after '%s' near '%s'\n",func_name,*expr);
                goto fail;
            }
            (*expr)++; // 跳过左括号
            
            char *args_tk=av_get_token(expr,")");
            if(!args_tk||*args_tk=='\0'||**expr!=')')
            {
                av_log(ctx->ind,AV_LOG_ERROR,"Expr preprocess:Unmatched '(' or invalid arguments in %s() function near '%s'\n",func_name,*expr);
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
static const char* expr_preprocess(cond_expr_evaluation_context *ctx,const char *expr)
{
    AVBPrint *bp=&ctx->ind->cond_expr_prep;
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
        av_log(ctx->ind,AV_LOG_ERROR,"Expr preprocess buffer overflow\n");
        goto fail;
    }
    
    return bp->str;
    
    fail:
    av_bprint_clear(bp);
    return "";
}

static double do_eval_cond(cond_expr_evaluation_context *ctx)
{
    const char *ori_expr=ctx->ind->cond_expr;
    const char *expr=expr_preprocess(ctx,ori_expr);
    
    AVExpr *expr_tree=NULL;
    
    int parse_ret=av_expr_parse(&expr_tree,expr,
                                ctx->var_names,
                                ctx->f1_names,ctx->f1_ptrs,
                                NULL,NULL,
                                0, ctx->ind);
    if(parse_ret<0)
    {
        av_log(ctx->ind, AV_LOG_ERROR,
               "Text expansion expression '%s' is not valid %d\n",
               expr,__LINE__);
    }
    
    double value=av_expr_eval(expr_tree,ctx->var_values,ctx);
    return value;
}

static int eval_condition(AVFilterLink *inlink,AVFrame *frm)
{
    cond_expr_evaluation_context eval_ctx=
    {
        .inlink=inlink,
        .ctx=inlink->dst,
        .ind=inlink->dst->priv,
        .frm=frm
    };
    fill_eval_context(&eval_ctx);
    double value=do_eval_cond(&eval_ctx);
    free_eval_context(&eval_ctx);
    return value;
}

static const char *expand_video_filter_desc(const char* ori_vf)
{
    return ori_vf;
}



static int execute_video_filter(AVFilterLink *inlink, AVFrame *frame,IndirectContext *ind,const char *vfilter)
{
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    int ret=0;
    FilterLink *inl=ff_filter_link(inlink);
    AVFilterGraph *graph=NULL;
    AVFilterInOut *outputs=NULL;
    AVFilterInOut *inputs=NULL;
    char *scale_sws_opts=NULL;
    AVBufferSrcParameters *par=NULL;
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    graph=avfilter_graph_alloc();
    if(!graph)
    {
        ret=AVERROR(ENOMEM);
        goto fail;
    }
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    graph->nb_threads=1;
    
    if(inl->graph&&inl->graph->scale_sws_opts)
    {
        scale_sws_opts=av_strdup(inl->graph->scale_sws_opts);
        graph->scale_sws_opts=scale_sws_opts;
    }
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    AVFilterContext *filt_src=avfilter_graph_alloc_filter(graph,avfilter_get_by_name("buffer"),"ffplay_buffer");
    if(!filt_src)
    {
        ret=AVERROR(ENOMEM);
        goto fail;
    }
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    par=av_buffersrc_parameters_alloc();
    par->format              = frame->format;
    par->time_base           = inlink->time_base;
    par->width               = frame->width;
    par->height              = frame->height;
    par->sample_aspect_ratio = inlink->sample_aspect_ratio;
    par->color_space         = frame->colorspace;
    par->color_range         = frame->color_range;
    par->alpha_mode          = frame->alpha_mode;
    par->frame_rate          = inl->frame_rate;
    par->hw_frames_ctx       = frame->hw_frames_ctx;
    ret=av_buffersrc_parameters_set(filt_src,par);
    if(ret<0)
        goto fail;
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    ret=avfilter_init_dict(filt_src,NULL);
    if(ret<0)
        goto fail;
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    AVFilterContext *filt_out=avfilter_graph_alloc_filter(graph,avfilter_get_by_name("buffersink"),"ffplay_buffersink");
    if(!filt_out)
    {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    int nb_pix_fmts=FF_ARRAY_ELEMS(pix_fmts);
    nb_pix_fmts--;//terminating AV_PIX_FMT_NONE
    if((ret=av_opt_set_array(filt_out,"pixel_formats",AV_OPT_SEARCH_CHILDREN,0,nb_pix_fmts,AV_OPT_TYPE_PIXEL_FMT,pix_fmts))<0)
        goto fail;
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    ret=avfilter_init_dict(filt_out,NULL);
    if(ret<0)
        goto fail;
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    
    int nb_filters=graph->nb_filters;
    outputs =  avfilter_inout_alloc();
    inputs  =  avfilter_inout_alloc();
    if(!outputs||!inputs)
    {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = filt_src;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;
    
    inputs->name        = av_strdup("out");
    inputs->filter_ctx  = filt_out;
    inputs->pad_idx     = 0;
    inputs->next        = NULL;
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    if((ret=avfilter_graph_parse_ptr(graph,vfilter,&inputs,&outputs,NULL))<0)
        goto fail;
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (int i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    ret=avfilter_graph_config(graph,NULL);
    if(ret<0)
        goto fail;
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    ret=av_buffersrc_add_frame(filt_src,frame);
    if(ret<0)
        goto fail;
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    ret=av_buffersink_get_frame_flags(filt_out,frame,0);
    if(ret<0)
        goto fail;
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);
    
    ret=ff_filter_frame(inlink->dst->outputs[0],frame);
    av_log(inlink->dst,AV_LOG_INFO,"execute_video_filter %d\n",__LINE__);

fail:
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    av_free(par);
    avfilter_graph_free(&graph);
    return ret;
}



static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
	AVFilterContext *ctx = inlink->dst;
	IndirectContext *indctx=ctx->priv;

    if(indctx->vf_desc&&eval_condition(inlink,frame))
    {
        av_log(indctx, AV_LOG_INFO, 
               "indirect triggered cond=%s, vf=%s\n",indctx->cond_expr,indctx->vf_desc);
        const char *vfilter=expand_video_filter_desc(indctx->vf_desc);
        return execute_video_filter(inlink,frame,indctx,vfilter);
    }
    
    return ff_filter_frame(ctx->outputs[0], frame);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args, char *res, int res_len, int flags)
{
	return 0;
}

#define OFFSET(x) offsetof(IndirectContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM


static const AVOption indirect_options[]=
{
	{ "cond",            "condition to toggle the indirect execution",           OFFSET(cond_expr),    AV_OPT_TYPE_STRING, { .str="never" },       0, 0,       FLAGS },
	{ "vf",              "set video filters",                                    OFFSET(vf_desc),      AV_OPT_TYPE_STRING, { .str=NULL  },       0, 0,       FLAGS },
    { "start_number",    "start frame number for n/frame_num variable",          OFFSET(start_number), AV_OPT_TYPE_INT,    { .i64=0       },       0, INT_MAX, FLAGS},
	{ NULL }
};

AVFILTER_DEFINE_CLASS(indirect);

static const AVFilterPad indirect_inputs[] = 
{
	{.name="default",.type=AVMEDIA_TYPE_VIDEO,.filter_frame=filter_frame},
};

const FFFilter ff_vf_indirect = {
	.p.name        = "indirect",
	.p.description = NULL_IF_CONFIG_SMALL("Dummy filter indirect filter"),
	.p.priv_class  = &indirect_class,
	.p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
	.priv_size     = sizeof(IndirectContext),
	.init          = init,
    .uninit        = uninit,
	FILTER_INPUTS(indirect_inputs),
	FILTER_OUTPUTS(ff_video_default_filterpad),
	FILTER_PIXFMTS_ARRAY(pix_fmts),
	.process_command = process_command,
};


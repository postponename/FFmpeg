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
#include "textutils.h"
#include "exprpreputil.h"
#include "textexpandutil.h"
#include <math.h>
#include <fenv.h>

#ifdef _WIN32
#include <compat/w32dlfcn.h>
#else
#include <dlfcn.h>
#endif

typedef struct
{
    const AVClass *class;
    AVBPrint item_name;
}IndirectInvokeeLogCtx;

static const char *indirect_invokee_item_name(void *ptr)
{
    IndirectInvokeeLogCtx *ctx=ptr;
    return ctx->item_name.str;
}

const AVClass indirect_invokee_class=
{
    .class_name = "indirect:invokee",
    .item_name  = indirect_invokee_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_FILTER,
};

static void *init_invoke_log_ctx(const char *path)
{
    IndirectInvokeeLogCtx *ctx=(IndirectInvokeeLogCtx*)av_mallocz(sizeof(IndirectInvokeeLogCtx));
    ctx->class=&indirect_invokee_class;
    av_bprint_init(&ctx->item_name,0,AV_BPRINT_SIZE_UNLIMITED);
    av_bprintf(&ctx->item_name,"%s / %s",ctx->class->class_name,path);
    return ctx;
}

static void uninit_invoke_log_ctx(void **ptr_ctx)
{
    IndirectInvokeeLogCtx *ctx=*ptr_ctx;
    av_bprint_finalize(&ctx->item_name,NULL);
    av_free(ctx);
    *ptr_ctx=NULL;
}

static IndirectInvokeContext *invoke_load(const char *path,char *stapar,int provide_log,void *ind_log_ctx)
{
    IndirectInvokeContext *ctx=(IndirectInvokeContext*)av_mallocz(sizeof(IndirectInvokeContext));
    
    ctx->path=path;
    ctx->lib=dlopen(path,0);
    if(!ctx->lib)
    {
        av_log(ind_log_ctx,AV_LOG_ERROR,"failed to load dynamic library '%s'\n",path);
        goto fail;
    }
    ctx->init=(InvokeInitFunc)dlsym(ctx->lib,"indirect_invoke_init");
    if(!ctx->init)
    {
        av_log(ind_log_ctx,AV_LOG_ERROR,"failed to load function 'indirect_invoke_init' from '%s'\n",path);
        goto fail;
    }
    ctx->vf=(InvokeVFFunc)dlsym(ctx->lib,"indirect_invoke_vf");
    if(!ctx->vf)
    {
        av_log(ind_log_ctx,AV_LOG_ERROR,"failed to load function 'indirect_invoke_vf' from '%s'\n",path);
        goto fail;
    }
    ctx->uninit=(InvokeUninitFunc)dlsym(ctx->lib,"indirect_invoke_uninit");
    if(!ctx->uninit)
    {
        av_log(ind_log_ctx,AV_LOG_ERROR,"failed to load function 'indirect_invoke_uninit' from '%s'\n",path);
        goto fail;
    }
    
    if(provide_log)
        ctx->log_ctx=init_invoke_log_ctx(path);
    else
        ctx->log_ctx=NULL;
    
    ctx->opaque=ctx->init(stapar,ctx->log_ctx);
    if(!ctx->opaque)
    {
        av_log(ind_log_ctx,AV_LOG_ERROR,"failed to initialize dynamic library '%s'\n",path);
        goto fail;
    }
    return ctx;
fail:
    if(!ctx)
        return NULL;
    if(ctx->log_ctx)
        uninit_invoke_log_ctx(&ctx->log_ctx);
    if(ctx->lib)
        dlclose(ctx->lib),
        ctx->lib=NULL;
    av_freep(&ctx);
    return NULL;
}
static const char *invoke_vf(IndirectInvokeContext *ctx,int argc,char **argv,void *ind_log_ctx)
{
    if(!ctx||!ctx->lib||!ctx->opaque||!ctx->vf)
        return NULL;
    int status=0;
    const char *vf=ctx->vf(ctx->opaque,argc,argv,&status);
    if(!status)
    {
        av_log(ind_log_ctx,AV_LOG_ERROR,"dynamic library '%s' failed to gnerate video filter desc\n",ctx->path);
        return NULL;
    }
    return vf;
}
static void invoke_unload(IndirectInvokeContext **ctx_ptr,void *ind_log_ctx)
{
    IndirectInvokeContext *ctx=*ctx_ptr;
    if(!ctx||!ctx->lib||!ctx->opaque||!ctx->uninit)
        return;
    ctx->uninit(&ctx->opaque);
    dlclose(ctx->lib);
    ctx->lib=NULL;
    if(ctx->log_ctx)
        uninit_invoke_log_ctx(&ctx->log_ctx);
    av_free(ctx);
    *ctx_ptr=NULL;
}


#define WHITESPACES " \n\t\r"

//the no-content-modifing version of av_get_token[avstring.c:143],and reserves  all '
static char *get_token_plain(const char **buf, const char *term)
{
    char *out     = av_realloc(NULL, strlen(*buf) + 1);
    char *ret     = out, *end = out;
    const char *p = *buf;
    if (!out)
        return NULL;
    p += strspn(p, WHITESPACES);
    
    while (*p && !strspn(p, term))
    {
        char c = *p++;
        if (c=='\\'&&*p)
        {
            *out++ = '\\';
            *out++ = *p++;
        }
        else if (c == '\'')
        {
            *out++ = '\'';
            while (*p && *p != '\'')
                *out++ = *p++;
            if (*p)
            {
                *out++ = '\'';
                p++;
                end = out;
            }
        } else {
            *out++ = c;
        }
    }
    
    do
        *out-- = 0;
    while (out >= end && strspn(out, WHITESPACES));
    
    *buf = p;
    
    char *small_ret = av_realloc(ret, out - ret + 2);
    return small_ret ? small_ret : ret;
}

static void init_invoke(IndirectContext *indctx)
{
    if(!indctx->invoke_cmd)
    {
        indctx->invoke_ctx=NULL;
        return;
    }
    const char *temp_cmd=indctx->invoke_cmd;
    indctx->invoke_path=av_get_token(&temp_cmd,":");
    temp_cmd++;//skip colomn
    if(!*temp_cmd)
        indctx->invoke_param=NULL;
    else
        indctx->invoke_param=get_token_plain(&temp_cmd,"");
    indctx->invoke_ctx=invoke_load(indctx->invoke_path,indctx->invoke_stapar,indctx->invoke_provide_log,indctx);
}

static void uninit_invoke(IndirectContext *indctx)
{
    if(!indctx->invoke_cmd)
        return;
    av_freep(&indctx->invoke_path);
    av_freep(&indctx->invoke_param);
    if(indctx->invoke_ctx)
        invoke_unload(&indctx->invoke_ctx,indctx);
}

static av_cold int init(AVFilterContext *ctx)
{
	IndirectContext *indctx = ctx->priv;
    memset(&indctx->ioplaying_global,0,sizeof(IOPlayingGlobal));
    memset(&indctx->playcall_global,0,sizeof(PlaycallGlobal));
    av_bprint_init(&indctx->expr_prep,0,AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&indctx->desc_cmd_expand,0,AV_BPRINT_SIZE_UNLIMITED);
    if(indctx->vf_desc&&indctx->invoke_cmd)
    {
        av_log(indctx,AV_LOG_ERROR,"cannot indicate both 'vf'(='%s') and 'invoke'(='%s')\n",indctx->vf_desc,indctx->invoke_cmd);
        return 0;
    }
    init_invoke(indctx);
	return 0;
}
static av_cold void uninit(AVFilterContext *ctx)
{
    IndirectContext *indctx = ctx->priv;
    av_bprint_finalize(&indctx->expr_prep,NULL);
    av_bprint_finalize(&indctx->desc_cmd_expand,NULL);
    uninit_invoke(indctx);
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
    VAR_NUMBER,
};

typedef struct
{
    AVFilterLink *inlink;
    AVFilterContext *ctx;
    IndirectContext *ind;
    AVFrame *frm;
    
    double pts;
    int frame_number;
    const char **var_names;
    double *var_values;
    const char **f1_names;
    double (**f1_ptrs)(void*,double);
    
}indirect_expansion_context;


static void fill_eval_context(indirect_expansion_context *ctx)
{
    AVFilterLink *inlink=ctx->inlink;
    FilterLink *ff_inl=ff_filter_link(ctx->inlink);
    ctx->pts=((ctx->frm->pts==AV_NOPTS_VALUE)?NAN:(ctx->frm->pts*av_q2d(ctx->inlink->time_base)));
    ctx->frame_number=ff_inl->frame_count_out+ctx->ind->start_number;
    
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
    
    ctx->f1_names=NULL;
    ctx->f1_ptrs=NULL;
}
static void free_eval_context(indirect_expansion_context *ctx)
{
    
}

static void expr_prep_metadata_call(void *c0,void *log_ctx,AVBPrint *bp,const char* func_name,const char *args)
{
    indirect_expansion_context *ctx=c0;
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

static double do_eval_expr(indirect_expansion_context *ctx,const char* ori_expr)
{
    FFExprPrepContext prep_ctx=
    {
        .opaque=ctx,
        .log_ctx=ctx->ind,
        .bp=&ctx->ind->expr_prep,
        .funcs=prep_func_table,
        .nb_funcs=FF_ARRAY_ELEMS(prep_func_table)
    };
    const char *expr=ff_expr_preprocess(&prep_ctx,ori_expr);
    
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
        return NAN;
    }
    
    double value=av_expr_eval(expr_tree,ctx->var_values,ctx);
    return value;
}

static int eval_condition(indirect_expansion_context *eval_ctx)
{    
    double ret=do_eval_expr(eval_ctx,eval_ctx->ind->cond_expr);
    if(isnan(ret))
        return 0;
    return ret!=0;
}

typedef struct
{
    const char *name;
    void (*expand)(void *ctx,AVBPrint *bp,const char *name,char **argv,int argc);
}indirect_expansion_func_entry;
static void expa_func_pict_type(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void expa_func_pts(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void expa_func_frame_num(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void expa_func_metadata(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void expa_func_strftime(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void expa_func_eval_expr(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void expa_func_eval_expr_int_fmt(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static void expa_func_if(void *c0,AVBPrint *bp,const char *name,char **argv,int argc);
static indirect_expansion_func_entry expa_func_table[]=
{
    {"pict_type",          expa_func_pict_type          },
    {"pts",                expa_func_pts                },
    {"frame_num",          expa_func_frame_num          },
    {"n",                  expa_func_frame_num          },
    {"metadata",           expa_func_metadata           },
    {"gmtime",             expa_func_strftime           },
    {"localtime",          expa_func_strftime           },
    {"expr",               expa_func_eval_expr          },
    {"e",                  expa_func_eval_expr          },
    {"expr_int_format",    expa_func_eval_expr_int_fmt  },
    {"eif",                expa_func_eval_expr_int_fmt  },
    {"if",                 expa_func_if                 },
};

static void expa_func_pict_type(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    indirect_expansion_context *ctx=c0;
    ff_expand_func_pict_type(ctx->ind,ctx->frm,bp,name,argv,argc);
}
static void expa_func_pts(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    indirect_expansion_context *ctx=c0;
    ff_expand_func_pts(ctx->ind,ctx->pts,bp,name,argv,argc);
}
static void expa_func_frame_num(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    indirect_expansion_context *ctx=c0;
    ff_expand_func_frame_num(ctx->ind,ctx->frame_number,bp,name,argv,argc);
}
static void expa_func_metadata(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    indirect_expansion_context *ctx=c0;
    const char *key_temp=argv[0];
    char *metakey=av_get_token(&key_temp,"");
    ff_expand_func_metadata(ctx->ind,ctx->frm,metakey,bp,name,argv,argc);
    av_freep(&metakey);
}
static void expa_func_strftime(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    indirect_expansion_context *ctx=c0;
    ff_expand_func_strftime(ctx->ind,bp,name,argv,argc);
}
static void expa_func_eval_expr(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    indirect_expansion_context *ctx=c0;
    const char *expr_temp=argv[0];
    char *expr=av_get_token(&expr_temp,"");
    double value=do_eval_expr(ctx,expr);
    av_freep(&expr);
    ff_expand_func_eval_expr(ctx->ind,value,bp,name,argv,argc);
}
static void expa_func_eval_expr_int_fmt(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    indirect_expansion_context *ctx=c0;
    const char *expr_temp=argv[0];
    char *expr=av_get_token(&expr_temp,"");
    double value=do_eval_expr(ctx,expr);
    av_freep(&expr);
    ff_expand_func_eval_expr_int_fmt(ctx->ind,value,bp,name,argv,argc);
}
static void expa_func_if(void *c0,AVBPrint *bp,const char *name,char **argv,int argc)
{
    indirect_expansion_context *ctx=c0;
    const char *expr_temp=argv[0];
    char *expr=av_get_token(&expr_temp,"");
    double value=do_eval_expr(ctx,expr);
    if(isnan(value))
        value=0;
    av_freep(&expr);
    ff_expand_func_if(ctx->ind,value,bp,name,argv,argc);
}

static void do_expand_function(indirect_expansion_context *ctx,AVBPrint *bp,char *name,char **argv,int argc)
{    
    int func=-1;
    int nb_out_func=FF_ARRAY_ELEMS(expa_func_table);
    
    for(int i=0;i<nb_out_func;i++)
    {
        if(strcmp(expa_func_table[i].name,name)==0)
        {
            func=i;
            break;
        }
    }
    if(func<0)
    {
        av_log(ctx->ind, AV_LOG_ERROR, 
               "Unknown out func '%s'\n",name);
        return;
    }
    
    expa_func_table[func].expand(ctx,bp,name,argv,argc);
}


static void expand_function(indirect_expansion_context *ctx,AVBPrint *bp,const char **pstr)
{
    const char *str=*pstr;
    av_assert0(*str=='$');
    
    int dcnt=0;
    while(*str=='$')
        dcnt++,++str;
    if(dcnt>1)
    {
        av_bprint_chars(bp,'$',dcnt-1);
        *pstr=str;
        return;
    }
    av_assert0(dcnt==1&&*str!='$');
    
    if(*str!='(')
    {
        av_bprint_chars(bp,'$',1);
        *pstr=str;
        return;
    }
    str++;
    
    char *argv[16];
    int argc=0;
    while(1)
    {
        if(!(argv[argc++]=get_token_plain(&str, "|)")))
        {
            av_log(ctx->ind,AV_LOG_ERROR,"av_get_token failed because of onmem\n");
            goto end;
        }
        if (!*str)
        {
            av_log(ctx->ind,AV_LOG_ERROR,"Unterminated $...$() near '%s'\n", *pstr);
            goto end;
        }
        if(argc==FF_ARRAY_ELEMS(argv))
            av_freep(&argv[--argc]);
        if(*str==')')
            break;
        str++;
    }
    
    *pstr=str+1;
    do_expand_function(ctx,bp,argv[0],argv+1,argc-1);
    
end:
    for(int i=0;i<argc;i++)
        av_freep(&argv[i]);
}
static const char *expand_dynstr(indirect_expansion_context *ctx,const char* ori)
{
    av_assert0(ori);
    AVBPrint *bp=&ctx->ind->desc_cmd_expand;
    av_bprint_clear(bp);
    
    while(*ori)
    {
        if(*ori=='$')
        {
            expand_function(ctx,bp,&ori);
        }
        else
        {
           av_bprint_chars(bp,*ori,1);
           ori++;
        }
    }
    if(!av_bprint_is_complete(bp))
    {
        av_log(ctx->ind,AV_LOG_ERROR,"bprint failed because of onmem\n");
    }
    
    
    return bp->str;
}

static const char *call_invoke_vf(indirect_expansion_context *ctx,IndirectInvokeContext *ivk,const char *param)
{
    if(!param)
    {
        return invoke_vf(ivk,0,NULL,ctx->ind);
    }
    
    const char *result=NULL;
    const char *expd=expand_dynstr(ctx,param);
    
    char *argv[16];
    int argc=0;
    while(1)
    {
        if(!(argv[argc++]=get_token_plain(&expd, ":")))
        {
            av_log(ctx->ind,AV_LOG_ERROR,"av_get_token failed because of onmem\n");
            goto end;
        }
        if(argc==FF_ARRAY_ELEMS(argv))
            av_freep(&argv[--argc]);
        if(!*expd)
            break;
        expd++;
    }
    
    result=invoke_vf(ivk,argc,argv,ctx->ind);
    
end:
    for(int i=0;i<argc;i++)
        av_freep(&argv[i]);
    
    return result;
}

static int indirect_ioplaying_check(IndirectContext *ind)
{
    return ind->ioplaying_global.window_w!=0;
}
static void indirect_fill_globals(IndirectContext *ind,AVFilterGraph *graph)
{
    if(!indirect_ioplaying_check(ind))
        return;
    for(int i=0;i<graph->nb_filters;i++)
    {
        AVFilterContext *fi=graph->filters[i];
        if(strcmp(fi->filter->name,"ioplaying")==0)
        {
            IOPlayingContext *iopctx=fi->priv;
            iopctx->global=ind->ioplaying_global;
        }
        if(strcmp(fi->filter->name,"indirect")==0)
        {
            IndirectContext *indctx=fi->priv;
            indctx->ioplaying_global=ind->ioplaying_global;
            indctx->playcall_global=ind->playcall_global;
        }
        if(strcmp(fi->filter->name,"playcall")==0)
        {
            PlaycallContext *pcctx=fi->priv;
            pcctx->global=ind->playcall_global;
        }
    }
}
static void indirect_transfer_playcall(IndirectContext *ind,AVFilterGraph *graph)
{
    PlaycallGlobal *pcg=&ind->playcall_global;
    if(!pcg->playcall_transferer||!pcg->opaque)
    {
        av_log(ind,AV_LOG_WARNING,"invalid playcall_transferer funcptr and opaque ptr for indirect");
        return;
    }
    pcg->playcall_transferer(pcg->opaque,graph);
}

static int execute_video_filter(AVFilterLink *inlink, AVFrame *frame,IndirectContext *ind,const char *vfilter)
{
    int ret=0;
    FilterLink *inl=ff_filter_link(inlink);
    AVFilterGraph *graph=NULL;
    AVFilterInOut *outputs=NULL;
    AVFilterInOut *inputs=NULL;
    char *scale_sws_opts=NULL;
    AVBufferSrcParameters *par=NULL;
    
    graph=avfilter_graph_alloc();
    if(!graph)
    {
        ret=AVERROR(ENOMEM);
        goto fail;
    }
    
    graph->nb_threads=1;
    
    if(inl->graph&&inl->graph->scale_sws_opts)
    {
        scale_sws_opts=av_strdup(inl->graph->scale_sws_opts);
        graph->scale_sws_opts=scale_sws_opts;
    }
    
    AVFilterContext *filt_src=avfilter_graph_alloc_filter(graph,avfilter_get_by_name("buffer"),"ffplay_buffer");
    if(!filt_src)
    {
        ret=AVERROR(ENOMEM);
        goto fail;
    }
    
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
    
    ret=avfilter_init_dict(filt_src,NULL);
    if(ret<0)
        goto fail;
    
    AVFilterContext *filt_out=avfilter_graph_alloc_filter(graph,avfilter_get_by_name("buffersink"),"ffplay_buffersink");
    if(!filt_out)
    {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    
    int nb_pix_fmts=FF_ARRAY_ELEMS(pix_fmts);
    nb_pix_fmts--;//terminating AV_PIX_FMT_NONE
    if((ret=av_opt_set_array(filt_out,"pixel_formats",AV_OPT_SEARCH_CHILDREN,0,nb_pix_fmts,AV_OPT_TYPE_PIXEL_FMT,pix_fmts))<0)
        goto fail;
    
    ret=avfilter_init_dict(filt_out,NULL);
    if(ret<0)
        goto fail;
    
    
    int nb_filters=graph->nb_filters;
    outputs =  avfilter_inout_alloc();
    inputs  =  avfilter_inout_alloc();
    if(!outputs||!inputs)
    {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = filt_src;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;
    
    inputs->name        = av_strdup("out");
    inputs->filter_ctx  = filt_out;
    inputs->pad_idx     = 0;
    inputs->next        = NULL;
    
    if((ret=avfilter_graph_parse_ptr(graph,vfilter,&inputs,&outputs,NULL))<0)
        goto fail;
    
    /* Reorder the filters to ensure that inputs of the custom filters are merged first */
    for (int i = 0; i < graph->nb_filters - nb_filters; i++)
        FFSWAP(AVFilterContext*, graph->filters[i], graph->filters[i + nb_filters]);
    
    ret=avfilter_graph_config(graph,NULL);
    if(ret<0)
        goto fail;
    
    indirect_fill_globals(ind,graph);
    
    ret=av_buffersrc_add_frame(filt_src,frame);
    if(ret<0)
        goto fail;
    
    ret=av_buffersink_get_frame_flags(filt_out,frame,0);
    if(ret<0)
        goto fail;

    indirect_transfer_playcall(ind,graph);
    
    ret=ff_filter_frame(inlink->dst->outputs[0],frame);
    
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
    if(indctx->wait_ioplaying&&!indirect_ioplaying_check(indctx))
    {
        av_log(indctx,AV_LOG_DEBUG,"context not initialized,do nothing\n");
        return ff_filter_frame(ctx->outputs[0],frame);
    }
    
    indirect_expansion_context expd_ctx=
    {
        .inlink=inlink,
        .ctx=ctx,
        .ind=indctx,
        .frm=frame
    };
    fill_eval_context(&expd_ctx);
    int cond=eval_condition(&expd_ctx);
    if(!cond || (indctx->vf_desc&&indctx->invoke_cmd) || (!indctx->vf_desc&&!indctx->invoke_cmd))
    {
        return ff_filter_frame(ctx->outputs[0],frame);
    }
    
    if(indctx->vf_desc)
    {
        const char *vfilter=expand_dynstr(&expd_ctx,indctx->vf_desc);        
        return execute_video_filter(inlink,frame,indctx,vfilter);
    }
    if(indctx->invoke_cmd)
    {
        const char *vfilter=call_invoke_vf(&expd_ctx,indctx->invoke_ctx,indctx->invoke_param);
        if(!vfilter)
            return ff_filter_frame(ctx->outputs[0],frame);
        return execute_video_filter(inlink,frame,indctx,vfilter);
    }
    free_eval_context(&expd_ctx);
    return 0;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args, char *res, int res_len, int flags)
{
	return 0;
}

#define OFFSET(x) offsetof(IndirectContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM


static const AVOption indirect_options[]=
{
	{ "cond",                  "condition to toggle the indirect execution",                                                          OFFSET(cond_expr),             AV_OPT_TYPE_STRING, { .str="0"     },       0, 0,       FLAGS },
	{ "vf",                    "set video filters.Exclusive with 'invoke'",                                                           OFFSET(vf_desc),               AV_OPT_TYPE_STRING, { .str=NULL    },       0, 0,       FLAGS },
    { "invoke",                "indicate to use dynamic library to generate vf desc.filename:param1:param2...Exclusive with 'vf'",    OFFSET(invoke_cmd),            AV_OPT_TYPE_STRING, { .str=NULL    },       0, 0,       FLAGS },
    { "invoke_stapar",         "indicate the initalize string param for dynamic library indicated by 'invoke'",                       OFFSET(invoke_stapar),         AV_OPT_TYPE_STRING, { .str=NULL    },       0, 0,       FLAGS },
    { "invoke_provide_log",    "whether indirect would provide a log context for the dynamic library.You MUST CONFIRM that "
                               "the ABI of ffmpeg compatity between the executor of this filter and the dynamic library if "
                               "you want to set this to 1",                                                                           OFFSET(invoke_provide_log),    AV_OPT_TYPE_INT,    { .i64=0,      },       0, INT_MAX, FLAGS },
    { "start_number",          "start frame number for n/frame_num variable",                                                         OFFSET(start_number),          AV_OPT_TYPE_INT,    { .i64=0       },       0, INT_MAX, FLAGS },
	{ "wait_ioplaying",        "weather the execution of video filters depends on the readility of the context for ioplaying ",       OFFSET(wait_ioplaying),        AV_OPT_TYPE_INT,    { .i64=1       },       0, INT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(indirect);

static const AVFilterPad indirect_inputs[] = 
{
	{.name="default",.type=AVMEDIA_TYPE_VIDEO,.filter_frame=filter_frame},
};

const FFFilter ff_vf_indirect = {
	.p.name        = "indirect",
	.p.description = NULL_IF_CONFIG_SMALL("Dummy filter for indirect dynamical apply filter"),
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


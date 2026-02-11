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

#include <math.h>
#include <fenv.h>

static av_cold int init(AVFilterContext *ctx)
{
	PlaycallContext *pcctx=ctx->priv;
    
	return 0;
}
static av_cold void uninit(AVFilterContext *ctx)
{
    PlaycallContext *pcctx=ctx->priv;

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

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
	AVFilterContext *ctx = inlink->dst;
	PlaycallContext *pcctx=ctx->priv;
    
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
	{ "cond",            "condition to toggle the play-relating call",                                                       OFFSET(cond_expr),         AV_OPT_TYPE_STRING, { .str="0"     },       0, 0,       FLAGS },
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


#include "config_components.h"

#include "libavutil/colorspace.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"
#include "libavutil/parseutils.h"
#include "libavutil/detection_bbox.h"
#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "video.h"
#include "ioplaying_interface.h"

static av_cold int init(AVFilterContext *ctx)
{
	IOPlayingContext *ioctx = ctx->priv;
    EventContent event=ioctx->event;
    printf("ioplaying init=event.key_state_map=%p:event.mouse_x=%d\n",(void*)event.key_state_map,event.mouse_x);
    
	return 0;
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

static int get_key(EventContent *event,SDL_Keycode key)
{
    unsigned char val=0;
    int res=ff_hashtable_get(event->key_state_map,&key,&val);
    if(!res)
        val=0;
    return val;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
	AVFilterContext *ctx = inlink->dst;
	IOPlayingContext *ioctx=ctx->priv;
    EventContent *event=&ioctx->event;
    if(get_key(event,SDLK_F2))
        printf("ioplaying key[F2] pressed=w=%s:event.mouse_x=%d\n",ioctx->w_expr,event->mouse_x);
    
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
	{ "x",         "set horizontal position of the left box edge", OFFSET(x_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
	{ "y",         "set vertical position of the top box edge",    OFFSET(y_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
	{ "width",     "set width of the box",                         OFFSET(w_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
	{ "w",         "set width of the box",                         OFFSET(w_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
	{ "height",    "set height of the box",                        OFFSET(h_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
	{ "h",         "set height of the box",                        OFFSET(h_expr),    AV_OPT_TYPE_STRING, { .str="0" },       0, 0, FLAGS },
	{ "thickness", "set the box thickness",                        OFFSET(t_expr),    AV_OPT_TYPE_STRING, { .str="3" },       0, 0, FLAGS },
	{ "t",         "set the box thickness",                        OFFSET(t_expr),    AV_OPT_TYPE_STRING, { .str="3" },       0, 0, FLAGS },
	{ NULL }
};

AVFILTER_DEFINE_CLASS(ioplaying);

static const AVFilterPad drawbox_inputs[] = 
{
	{.name="default",.type=AVMEDIA_TYPE_VIDEO,.filter_frame=filter_frame},
};

const FFFilter ff_vf_ioplaying = {
	.p.name        = "ioplaying",
	.p.description = NULL_IF_CONFIG_SMALL("ioplaying filter!dummy filter"),
	.p.priv_class  = &ioplaying_class,
	.p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
	.priv_size     = sizeof(IOPlayingContext),
	.init          = init,
	FILTER_INPUTS(drawbox_inputs),
	FILTER_OUTPUTS(ff_video_default_filterpad),
	FILTER_PIXFMTS_ARRAY(pix_fmts),
	.process_command = process_command,
};


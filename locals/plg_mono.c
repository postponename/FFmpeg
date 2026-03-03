#include<libavutil/bprint.h>
#include<libavutil/avstring.h>
#include<libavutil/mem.h>
#include<libavutil/log.h>
#include<stdlib.h>
#include<string.h>

__declspec(dllexport) void *indirect_invoke_init(char *stapar,void *log_ctx);
__declspec(dllexport) void indirect_invoke_uninit(void **ptr_opaque);
__declspec(dllexport) const char * indirect_invoke_vf(void *opaque,int argc,char **argv,int *status);

typedef struct
{
    AVBPrint gen_vf;
    char *initpar;
    void *log_ctx;
}PlgAVBPrintContext;

void *indirect_invoke_init(char *stapar,void *log_ctx)
{
    PlgAVBPrintContext *ctx=(PlgAVBPrintContext*)av_mallocz(sizeof(PlgAVBPrintContext));
    ctx->log_ctx=log_ctx;
    av_bprint_init(&ctx->gen_vf,0,AV_BPRINT_SIZE_UNLIMITED);
    ctx->initpar=av_strdup(stapar);
    return ctx;
}
void indirect_invoke_uninit(void **ptr_opaque)
{
    PlgAVBPrintContext *ctx=*ptr_opaque;
    av_bprint_finalize(&ctx->gen_vf,NULL);
    av_freep(&ctx->initpar);
    av_free(ctx);
    *ptr_opaque=NULL;
}

void push_bp_args(AVBPrint *bp,int argc,char **argv,const char *sep,const char *defval)
{
    if(argc<=0)
    {
        av_bprintf(bp,"%s",defval?defval:"");
    }
    else if(argc==1)
    {
        av_bprintf(bp,"%s",argv[0]);
    }
    else
    {
        for(int i=0;i<argc-1;i++)
            av_bprintf(bp,"%s%s",argv[i],sep?sep:"");
        av_bprintf(bp,"%s",argv[argc-1]);
    }
}

const char * indirect_invoke_vf(void *opaque,int argc,char **argv,int *status)
{
    PlgAVBPrintContext *ctx=opaque;
    AVBPrint *bp=&ctx->gen_vf;
    av_bprint_clear(bp);
    av_bprintf(bp,
R"+++(
drawtext=
    text='fixedtext | )+++");
    av_bprintf(bp,"%s | ",ctx->initpar?ctx->initpar:"(no stapar)");
    push_bp_args(bp,argc,argv," | ","(no args)");
    av_bprintf(bp,R"+++(':
    x=1:
    y=1+3*main_h*0.017:
    fontsize=main_h*0.017:
    fontcolor=white:
    fontfile='C:/Windows/Fonts/simhei.ttf':
    box=1:
    boxcolor=black@0.5,
)+++");

    *status=1;
    return bp->str;
}

#include "config_components.h"
#include "exprpreputil.h"
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
#include "ioplaying_interface.h"
#include "textutils.h"
#include <math.h>
#include <fenv.h>


static void skip_whitespace(const char **expr)
{
    while (*expr && av_isspace(**expr)) {
        (*expr)++;
    }
}
static int match_do_prep_funcs(FFExprPrepContext *ctx,const char **expr,AVBPrint *bp)
{
    int nb_prep_func=ctx->nb_funcs;
    int matched=0;
    
    for(int i=0;i<nb_prep_func;i++)
    {
        const char *func_name=ctx->funcs[i].name;
        int func_len=strlen(func_name);
        
        if(strncmp(*expr,func_name,func_len)==0)
        {
            matched=1;
            (*expr)+=func_len;
            
            skip_whitespace(expr); // 先跳过括号前的空白
            if(**expr!='(')
            {
                av_log(ctx->log_ctx,AV_LOG_ERROR,"Expr preprocess: Expected '(' after '%s' near '%s'\n",func_name,*expr);
                goto fail;
            }
            (*expr)++; // 跳过左括号
            
            char *args_tk=av_get_token(expr,")");
            if(!args_tk||*args_tk=='\0'||**expr!=')')
            {
                av_log(ctx->log_ctx,AV_LOG_ERROR,"Expr preprocess:Unmatched '(' or invalid arguments in %s() function near '%s'\n",func_name,*expr);
                av_freep(&args_tk);
                goto fail;
            }
            (*expr)++;// 跳过右括号
            
            ctx->funcs[i].prep(ctx->opaque,ctx->log_ctx,bp,func_name,args_tk);
            av_freep(&args_tk);
            
            break;
        }
    }
    
    return matched;
    fail:
    return -1;
}
const char* ff_expr_preprocess(FFExprPrepContext *ctx,const char *expr)
{
    AVBPrint *bp=ctx->bp;
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
        av_log(ctx->log_ctx,AV_LOG_ERROR,"Expr preprocess buffer overflow\n");
        goto fail;
    }
    
    return bp->str;
    
    fail:
    av_bprint_clear(bp);
    return "";
}


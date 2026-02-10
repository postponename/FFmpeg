#ifndef TEXTEXPANDUTIL_H
#define TEXTEXPANDUTIL_H
#include "avfilter.h"
#include "libavcodec/hashtable.h"
#include "libavutil/eval.h"
#include "libavutil/bprint.h"
#include "libavutil/mem.h"
#include "textutils.h"

void ff_expand_func_pict_type(void *log_ctx,AVFrame *frm,AVBPrint *bp,const char *name,char **argv,int argc);
void ff_expand_func_pts(void *log_ctx,double pts,AVBPrint *bp,const char *name,char **argv,int argc);
void ff_expand_func_frame_num(void *log_ctx,int frame_num,AVBPrint *bp,const char *name,char **argv,int argc);
void ff_expand_func_metadata(void *log_ctx,AVFrame *frm,AVBPrint *bp,const char *name,char **argv,int argc);
void ff_expand_func_strftime(void *log_ctx,AVBPrint *bp,const char *name,char **argv,int argc);
void ff_expand_func_eval_expr(void *log_ctx,double value,AVBPrint *bp,const char *name,char **argv,int argc);
void ff_expand_func_eval_expr_int_fmt(void *log_ctx,double value,AVBPrint *bp,const char *name,char **argv,int argc);
void ff_expand_func_if(void *log_ctx,double value,AVBPrint *bp,const char *name,char **argv,int argc);

#endif

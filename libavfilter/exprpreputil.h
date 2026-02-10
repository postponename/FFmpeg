#ifndef EXPRPREPUTIL_H
#define EXPRPREPUTIL_H
#include "avfilter.h"
#include "libavcodec/hashtable.h"
#include "libavutil/eval.h"
#include "libavutil/bprint.h"
#include "libavutil/mem.h"

typedef struct
{
    const char *name;
    void (*prep)(void *opa,void *log_ctx,AVBPrint *bp,const char* name,const char *args);
}FFExprPreprocer;
typedef struct
{
    void *opaque;
    void *log_ctx;
    AVBPrint *bp;
    FFExprPreprocer *funcs;
    int nb_funcs;
}FFExprPrepContext;

const char* ff_expr_preprocess(FFExprPrepContext *ctx,const char *expr);

#endif

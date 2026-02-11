#include"textexpandutil.h"
#include <math.h>
#include <fenv.h>

void ff_expand_func_pict_type(void *log_ctx,AVFrame *frm,AVBPrint *bp,const char *name,char **argv,int argc)
{
    av_bprintf(bp,"%c",av_get_picture_type_char(frm->pict_type));
}
void ff_expand_func_pts(void *log_ctx,double pts,AVBPrint *bp,const char *name,char **argv,int argc)
{
    const char *fmt;
    const char *strftime_fmt = NULL;
    const char *delta = NULL;
    
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
                av_log(log_ctx,AV_LOG_WARNING,"pts third argument 24HH is deprecated, use pts:hms24hh instead\n");
                fmt="hms24";
            }
            else
            {
                av_log(log_ctx,AV_LOG_ERROR,"Invalid argument '%s', '24HH' was expected\n",argv[2]);
                return;
            }
        }
        else
        {
            strftime_fmt=argv[2];
        }
    }
    
    int res=ff_print_pts(log_ctx, bp, pts, delta, fmt, strftime_fmt);
    if(res<0)
    {
        av_log(log_ctx,AV_LOG_ERROR,"Failed to print pts, funcname=%s,pts=%f,delta=%s,fmt=%s,strftime_fmt=%s\n",name,pts,delta,fmt,strftime_fmt);
    }
}
void ff_expand_func_frame_num(void *log_ctx,int frame_num,AVBPrint *bp,const char *name,char **argv,int argc)
{
    av_bprintf(bp,"%d",frame_num);
}
void ff_expand_func_metadata(void *log_ctx,AVFrame *frm,const char *key,AVBPrint *bp,const char *name,char **argv,int argc)
{
    char *defval=NULL;
    if(argc>=2)
        defval=argv[1];
    
    AVDictionaryEntry *e=av_dict_get(frm->metadata,key,NULL,0);
    
    if (e&&e->value)
        av_bprintf(bp,"%s",e->value);
    else if(defval)
        av_bprintf(bp,"%s",defval);
}
void ff_expand_func_strftime(void *log_ctx,AVBPrint *bp,const char *name,char **argv,int argc)
{
    const char *strftime_fmt = argc ? argv[0] : NULL;
    
    int res=ff_print_time(log_ctx,bp,strftime_fmt,strcmp(name,"localtime")==0);
    if(res<0)
    {
        av_log(log_ctx,AV_LOG_ERROR,"Failed to print time, funcname=%s,strftime_fmt=%s\n",name,strftime_fmt);
    }
}
void ff_expand_func_eval_expr(void *log_ctx,double value,AVBPrint *bp,const char *name,char **argv,int argc)
{
    //argv[0] : expr which has been evaluated as 'value'
    av_bprintf(bp, "%f", value);
}
void ff_expand_func_eval_expr_int_fmt(void *log_ctx,double value,AVBPrint *bp,const char *name,char **argv,int argc)
{
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
            av_log(log_ctx,AV_LOG_ERROR,"expr_int_format(): Invalid number of positions to print: '%s'\n", argv[2]);
            return;
        }
    }
    
    if (!strchr("xXdu", format))
    {
        av_log(log_ctx, AV_LOG_ERROR, "Invalid format '%c' specified,"
               " allowed values: 'x', 'X', 'd', 'u'\n", format);
        return;
    }
    
    feclearexcept(FE_ALL_EXCEPT);
    int intval=value;
#if defined(FE_INVALID) && defined(FE_OVERFLOW) && defined(FE_UNDERFLOW)
    int fetestexcept_ret;
    if ((fetestexcept_ret =fetestexcept(FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW)))
    {
        av_log(log_ctx, AV_LOG_ERROR, "Conversion of floating-point result to int failed. Control register: 0x%08x. Conversion result: %d\n", fetestexcept_ret, intval);
        return;
    }
#endif
    char fmt_str[30] = "%";
    
    if (positions >= 0)
        av_strlcatf(fmt_str, sizeof(fmt_str), "0%u", positions);
    av_strlcatf(fmt_str, sizeof(fmt_str), "%c", format);
    
    av_log(log_ctx, AV_LOG_DEBUG, "Formatting value %f (expr '%s') with spec '%s'\n",
           value, expr, fmt_str);
    
    av_bprintf(bp, fmt_str, intval);
}
void ff_expand_func_if(void *log_ctx,double value,AVBPrint *bp,const char *name,char **argv,int argc)
{
    //argv[0] : expr which has been evaluated as 'value'
    char *true_str=argv[1];
    char *false_str=NULL;
    if(argc>=3)
        false_str=argv[2];
    
    if(value!=0)
        av_bprintf(bp,"%s",true_str);
    else if(false_str)
        av_bprintf(bp,"%s",false_str);
}

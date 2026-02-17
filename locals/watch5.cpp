#include<windows.h>
#include<iostream>
#include<vector>
#include<format>
#include<ranges>
#include<print>
using namespace std::literals;

std::pair<int,int> parse_wh(const std::string& whs)
{
    auto p0=whs.find('x');
    if(p0==whs.npos||p0==0||p0==whs.size()-1)
        throw std::invalid_argument(std::format("Unacceptable WH string '{}', it must be WxH",whs));
    return {std::stoi(whs.substr(0,p0)),std::stoi(whs.substr(p0+1))};
}
double parse_time(const std::string& tis)
{
    auto p0=tis.find('.');
    std::string integ,frac="0";
    if(p0!=tis.npos)
        integ=tis.substr(0,p0),
        frac.append(tis.substr(p0));
    else
        integ=tis;
    
    using namespace std::views;
    using std::ranges::to;
    std::vector<std::string> segs=
        integ|
        split(':')|
        to<std::vector<std::string>>();
    
    int ss=0,mm=0,hh=0;
    if(segs.size()==1)//ss
        ss=std::stoi(segs[0]);
    else if(segs.size()==2)//mm:ss
        mm=std::stoi(segs[0]),
        ss=std::stoi(segs[1]);
    else if(segs.size()==3)//hh:mm:ss
        hh=std::stoi(segs[0]),
        mm=std::stoi(segs[1]),
        ss=std::stoi(segs[2]);
    else
        throw std::invalid_argument(std::format("Unacceptable time string '{}', it must be hh:mm:ss.frac",tis));
    
    return hh*3600+mm*60+ss+std::stod(frac);
}
bool parse_is_debug(const std::string& str)
{
    if(str.empty())
        return false;
    return str=="D"||str=="Debug";
}
std::wstring utf82wchar(const std::string& utf8_str)
{
    if(utf8_str.empty())
        return L"";
    
    int wchar_len=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8_str.c_str(),-1,nullptr,0);
    
    if(wchar_len==0)
        throw std::runtime_error(std::format("failed to convert from utf8 to utf16 : errcode:{}",GetLastError()));
    
    std::wstring wstr(wchar_len,L'\0');
    int ret=MultiByteToWideChar
    (
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8_str.c_str(),
        -1,
        wstr.data(),
        wchar_len
    );
    
    if(ret==0)
        throw std::runtime_error(std::format("failed to convert from utf8 to utf16 : errcode:{}",GetLastError()));
    
    return wstr;
}
void sink2process(const std::string& cmd)
{
    STARTUPINFOW si{};
    si.cb=sizeof(STARTUPINFOW);
    PROCESS_INFORMATION pi{};
    
    std::wstring wcmd=utf82wchar(cmd);
    std::vector<wchar_t> cmd_buf{wcmd.begin(),wcmd.end()};
    BOOL ok=CreateProcessW
    (
        NULL,
        cmd_buf.data(),
        NULL,NULL,
        FALSE,
        0,                   //要无窗口用CREATE_NO_WINDOW
        NULL,NULL,
        &si,&pi
    );
    
    if(!ok)
        throw std::runtime_error(std::format("CreateProcessW Failed : errcode:{}",GetLastError()));
    
    WaitForSingleObject(pi.hProcess,INFINITE);
    
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}
void go_ffplay(bool is_debug,double start_time,int window_height,const std::string& video_path,const std::string& video_filter)
{
    std::string cmd;
    if(is_debug)
        cmd="gdb --args \"E:\\ffmpeg-custom\\ffplay_g.exe\" ";
    else
        cmd="ffplay ";
    
    cmd.append(std::format("-ss {} ",start_time));
    cmd.append("-noborder ");
    cmd.append(std::format("-y {} ",window_height));
    cmd.append(std::format("-i \"{}\" ",video_path));
    cmd.append(std::format("-vf \"{}\" ",video_filter));
    
//    std::println("启动命令行: {}",cmd);
    sink2process(cmd);
}

double get_start_time()
{
    return 6944;
}
std::string get_video_filter(double video_time)
{
    return std::format
(R"+++(
ioplaying=
    out='metadata:yggi,'='八千代小姐我是你的狗 | %{{expr:t}} | {0}':
    cond='key:v:press',
ioplaying=
    cond='all':
    out='metadata:meta_one'='1',
ioplaying=
    cond='once':
    out='var:last_mlb'='0',
ioplaying=
    cond=once:
    out='var:selecting'='0',
ioplaying=
    cond=once:
    out='var:sel_start_x'='0',
ioplaying=
    cond=once:
    out='var:sel_start_y'='0',
ioplaying=
    cond='mouse:left:press':
    out='var:selecting'='%{{expr:if((1-last_mlb)*(1-selecting),1,selecting)}}',
ioplaying=
    cond='mouse:left:release':
    out='var:selecting'='%{{expr:if(selecting,0*key(K)*metadata(meta_one),selecting)}}',
ioplaying=
    cond='mouse:left:press':
    out='var:sel_start_x'='%{{expr:if((1-last_mlb),main_w*x/scr_w,sel_start_x)}}',
ioplaying=
    cond='mouse:left:press':
    out='var:sel_start_y'='%{{expr:if((1-last_mlb),main_h*y/scr_h,sel_start_y)}}',

ioplaying=
    out='var:last_mlb'='%{{expr:mouse_left}}':
    cond='all',

ioplaying=
    cond=all:
    out='metadata:meta_sel'='%{{expr:selecting}}',
ioplaying=
    cond=all:
    out='metadata:meta_selbox_x'='%{{expr:min(sel_start_x,main_w*x/scr_w)}}',
ioplaying=
    cond=all:
    out='metadata:meta_selbox_y'='%{{expr:min(sel_start_y,main_h*y/scr_h)}}',
ioplaying=
    cond=all:
    out='metadata:meta_selbox_w'='%{{expr:max(1,max(sel_start_x,main_w*x/scr_w)-min(sel_start_x,main_w*x/scr_w))}}',
ioplaying=
    cond=all:
    out='metadata:meta_selbox_h'='%{{expr:max(1,max(sel_start_y,main_h*y/scr_h)-min(sel_start_y,main_h*y/scr_h))}}',

ioplaying=
    cond=once:
    out='var:last_f1'=0,
ioplaying=
    cond=once:
    out='var:show_info'='0',
ioplaying=
    cond='key:f1:press':
    out='var:show_info'='%{{expr:if(1-last_f1,1-show_info,show_info)}}',
ioplaying=
    cond=all:
    out='var:last_f1'='%{{expr:key(f1)}}',

ioplaying=
    cond=all:
    out='metadata:meta_info_show'='%{{expr:show_info}}',
ioplaying=
    cond=all:
    out='metadata:meta_volume'='%{{expr:audio_volume}}',


ioplaying=
    out='metadata:lmbpr'='%{{expr:mouse_left}}':
    cond='all',
ioplaying=
    out='metadata:mouse_x'='%{{expr:main_w*x/scr_w}}':
    cond='mouse:left:press',
ioplaying=
    out='metadata:mouse_y'='%{{expr:main_h*y/scr_h}}':
    cond='mouse:left:press',
ioplaying=
    out='metadata:meta_mouse_x_pts'='%{{expr:({0})*x/scr_w}}':
    cond='mouse:left:press',

indirect=
    vf='
        ioplaying=
            cond=once:
            out='\''print'\''='\''ONCE IN INDIRECT'\'',
        
    ':
    cond='1',
indirect=
    vf='
        ioplaying=
            cond='\''all'\'':
            out='\''metadata:yyi,'\''='\''你看这个'\'',
        drawtext=
            text=$(if|'\''metadata(lmbpr)'\''|%{{metadata\:yggi\,}}  C\:/Windows/Fonts/simhei.ttf):
            x=$(expr|1+1-2*2+3):
            y=$(metadata|mouse_y|1):
            fontsize=$(expr|main_h)*0.017:
            fontcolor=white:
            box=1:
            boxcolor=black@0.5:
            fontfile='\''C:/Windows/Fonts/simhei.ttf'\'',
        playcall
    ':
    cond='metadata(lmbpr)*0',

indirect=
    vf='
        drawtext=
            text='\''%{{eif:($(metadata|meta_mouse_x_pts))/3600:d}}:%{{eif:mod(($(metadata|meta_mouse_x_pts)),3600)/60:d}}:%{{eif:mod(($(metadata|meta_mouse_x_pts)),60):d}} / %{{eif:({0})/3600:d}}:%{{eif:mod(({0}),3600)/60:d}}:%{{eif:mod(({0}),60):d}}'\'':
            x=(w-text_w)/2:y=(h-text_h)/2:
            fontsize=$(expr|main_h)*0.05:
            fontcolor=white:
            box=1:
            boxcolor=black@0.5:
            fontfile='\''C:/Windows/Fonts/simhei.ttf'\'',
        
    ':
    cond='metadata(lmbpr)',

ioplaying=
    cond=all:
    out='metadata:meta_f2'='%{{expr:key(f2)}}',
ioplaying=
    cond=all:
    out='metadata:meta_f3'='%{{expr:key(f3)}}',
ioplaying=
    cond=all:
    out='metadata:meta_f4'='%{{expr:key(f4)}}',
ioplaying=
    cond=all:
    out='metadata:meta_f5'='%{{expr:key(f5)}}',
ioplaying=
    cond=all:
    out='metadata:meta_mouse_x_ratio'='%{{expr:x/scr_w}}',
indirect=
    vf='
        playcall=
            call='\''seek:-100:rel:frame'\'',
    ':
    cond='metadata(meta_f2)',
indirect=
    vf='
        playcall=
            call='\''volume:-0.005:rel'\'',
    ':
    cond='metadata(meta_f3)',
indirect=
    vf='
        playcall=
            call='\''mute:switch'\'',
    ':
    cond='metadata(meta_f4)',
indirect=
    vf='
        playcall=
            call='\''seek:$(expr|'\''metadata(meta_mouse_x_ratio)'\''):abs:ratio'\'',
    ':
    cond='metadata(meta_f5)',

indirect=
    vf='
        drawbox=
            t=fill:
            x=$(metadata|meta_selbox_x|1):
            y=$(metadata|meta_selbox_y|1):
            w=$(metadata|meta_selbox_w|1):
            h=$(metadata|meta_selbox_h|1):
            color=0x66CCFF@0.5,
    ':
    cond='metadata(meta_sel)*0',

ioplaying=
    cond=all:
    out='var:progbar_ratio'='0.005',
ioplaying=
    cond=all:
    out='metadata:progbar_x'='0',
ioplaying=
    cond=all:
    out='metadata:progbar_y'='%{{expr:main_h*(1-progbar_ratio)}}',
ioplaying=
    cond=all:
    out='metadata:progbar_w'='%{{expr:main_w*t/{0}}}',
ioplaying=
    cond=all:
    out='metadata:progbar_h'='%{{expr:main_h*progbar_ratio}}',
indirect=
    vf='
        drawbox=
        t=fill:
        x=$(metadata|progbar_x|1):
        y=$(metadata|progbar_y|1):
        w=$(metadata|progbar_w|1):
        h=$(metadata|progbar_h|1):
        color=0x66CCFF@0.2,
    ':
    cond='1',
ioplaying=
    cond=all:
    out='metadata:frame_number_4_indirect'='%{{n}}',
indirect=
    vf='
        drawtext=
            text='\''%{{pts:hms}} | %{{expr:100*t/({0})}}\% | $(metadata|frame_number_4_indirect) | %{{metadata:meta_volume}}'\'':
            x=1:
            y=1:
            fontsize=main_h*0.017:
            fontcolor=white:
            fontfile='\''C:/Windows/Fonts/simhei.ttf'\'':
            box=1:
            boxcolor=black@0.5,
        drawtext=
            text='\''%{{localtime}} | 新年快乐'\'':
            x=1:
            y=1+main_h*0.017:
            fontsize=main_h*0.017:
            fontcolor=white:
            fontfile='\''C:/Windows/Fonts/simhei.ttf'\'':
            box=1:
            boxcolor=black@0.5,

    ':
    cond='metadata(meta_info_show)'

)+++",video_time);
}






int main(int argc,char **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    if(argc<5)
    {
        printf("用法: %s <视频路径> <时长hh:mm:ss> <宽x高> <窗口高度> [D,Debug]\n", argv[0]);
        printf("示例: %s video.mp4 01:23:45.67 1920x1080 720 D\n", argv[0]);
        return 0;
    }
    std::string video_path=argv[1];
    std::string video_time_str=argv[2];
    std::string video_wh_str=argv[3];
    std::string window_height_str=argv[4];
    std::string is_debug_str;
    if(argc>=6)
        is_debug_str=argv[5];
    
    auto [w,h]=parse_wh(video_wh_str);
    auto video_secs=parse_time(video_time_str);
    int window_height=std::stoi(window_height_str);
    bool is_debug=parse_is_debug(is_debug_str);
    
    std::println("视频路径：{}",video_path);
    std::println("视频宽：{}",w);
    std::println("视频高：{}",h);
    std::println("视频长度（秒数）：{}",video_secs);
    std::println("窗口高度：{}",window_height);
    std::println("是否Debug：{}",is_debug);
    
    std::string cmd;
    if(is_debug)
        cmd="gdb --args \"E:\\ffmpeg-custom\\ffplay_g.exe\" ";
    else
        cmd="ffplay ";
    
    double start_time=get_start_time();

    std::string video_filter=get_video_filter(video_secs);
    
    go_ffplay(is_debug,start_time,window_height,video_path,video_filter);
    return 0;
}

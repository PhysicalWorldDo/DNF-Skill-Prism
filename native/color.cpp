#include "prism_core.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <set>

namespace prism {
namespace {
using RGB=std::array<double,3>;
RGB hex(const std::string& value){if(value.size()!=7||value[0]!='#'||value.find_first_not_of("#0123456789abcdefABCDEF")!=std::string::npos)throw rep::Error("Invalid RGB hex color");return {double(std::stoi(value.substr(1,2),nullptr,16)),double(std::stoi(value.substr(3,2),nullptr,16)),double(std::stoi(value.substr(5,2),nullptr,16))};}
RGB hsv(double h,double s,double v){h=std::fmod(h+720,360);double c=v*s,x=c*(1-std::abs(std::fmod(h/60,2)-1)),m=v-c;RGB result=h<60?RGB{c,x,0}:h<120?RGB{x,c,0}:h<180?RGB{0,c,x}:h<240?RGB{0,x,c}:h<300?RGB{x,0,c}:RGB{c,0,x};for(auto& channel:result)channel=(channel+m)*255;return result;}
RGB toHsv(RGB rgb){double top=*std::max_element(rgb.begin(),rgb.end()),bottom=*std::min_element(rgb.begin(),rgb.end()),delta=top-bottom,h=0;if(delta){if(top==rgb[0])h=(rgb[1]-rgb[2])/delta;else if(top==rgb[1])h=(rgb[2]-rgb[0])/delta+2;else h=(rgb[0]-rgb[1])/delta+4;h=std::fmod(h*60+360,360);}return {h,top?delta/top:0,top/255};}
uint8_t byte(double v){return uint8_t(std::clamp(std::lround(v),0L,255L));}
RGB gradient(const std::vector<RGB>& colors,double t){t=std::clamp(t,0.,1.)*(colors.size()-1);size_t i=std::min(colors.size()-2,size_t(t));double f=t-i;RGB result{};for(int c=0;c<3;c++)result[c]=colors[i][c]*(1-f)+colors[i+1][c]*f;return result;}
}
ColorSettings colorSettings(const Json& value){
    if(!value.is_object())throw rep::Error("Color settings must be an object");ColorSettings result;
    result.mode=value.value("mode",result.mode);result.colors=value.value("colors",result.colors);
    static const std::set<std::string> modes{"none","hue","gradient","shift","tint","gray_map","max_rgb","cosine_grad","glitch","line_art","transparent"};
    if(!modes.contains(result.mode))throw rep::Error("Unsupported color mode");
    if(result.colors.size()<2||result.colors.size()>32)throw rep::Error("A palette needs 2 to 32 colors");for(auto& color:result.colors)hex(color);
    auto number=[&](const char* key,double fallback,double minimum,double maximum){double v=value.value(key,fallback);if(!std::isfinite(v)||v<minimum||v>maximum)throw rep::Error(std::string("Invalid color parameter: ")+key);return v;};
    result.hue=number("hue",result.hue,0,360);result.saturation=number("saturation",result.saturation,0,200);result.brightness=number("brightness",result.brightness,0,200);result.intensity=number("intensity",result.intensity,0,100);
    result.preserveAlpha=value.value("preserveAlpha",true);result.preserveBlack=value.value("preserveBlack",true);result.preserveHighlight=value.value("preserveHighlight",true);
    result.overlay=value.value("overlay",result.overlay);if(!std::set<std::string>{"none","outer_glow","lightning","starlight","sakura"}.contains(result.overlay))throw rep::Error("Unsupported overlay");
    result.density=number("density",result.density,0,100);result.scale=number("scale",result.scale,.1,5);result.frequency=number("frequency",result.frequency,.01,20);result.offset=number("offset",result.offset,0,1);result.seed=value.value("seed",1U);return result;
}
Json colorJson(const ColorSettings& s){return {{"mode",s.mode},{"colors",s.colors},{"hue",s.hue},{"saturation",s.saturation},{"brightness",s.brightness},{"intensity",s.intensity},{"preserveAlpha",s.preserveAlpha},{"preserveBlack",s.preserveBlack},{"preserveHighlight",s.preserveHighlight},{"overlay",s.overlay},{"density",s.density},{"scale",s.scale},{"frequency",s.frequency},{"offset",s.offset},{"seed",s.seed}};}
rep::Bytes recolor(std::span<const uint8_t> rgba,int width,int height,const ColorSettings& s){
    if(width<=0||height<=0||rgba.size()!=size_t(width)*height*4)throw rep::Error("Invalid RGBA frame dimensions");
    rep::Bytes output(rgba.begin(),rgba.end());if(s.intensity==0||(s.mode=="none"&&s.overlay=="none"))return output;
    std::vector<RGB> colors;for(auto& color:s.colors)colors.push_back(hex(color));double blend=s.intensity/100;
    auto source=[&](int x,int y,int c)->double{if(x<0||x>=width||y<0||y>=height)return 0;return rgba[(size_t(y)*width+x)*4+c];};
    for(int y=0;y<height;y++)for(int x=0;x<width;x++){
        size_t i=(size_t(y)*width+x)*4;if(!rgba[i+3])continue;RGB rgb{double(rgba[i]),double(rgba[i+1]),double(rgba[i+2])};auto oldHsv=toHsv(rgb);double maximum=oldHsv[2]*255,minimum=*std::min_element(rgb.begin(),rgb.end());
        if(s.mode!="transparent"&&((s.preserveBlack&&maximum==0)||(s.preserveHighlight&&minimum>=250)))continue;
        RGB target=rgb;double alpha=rgba[i+3];
        if(s.mode=="hue")target=hsv(s.hue,std::min(1.,oldHsv[1]*s.saturation/100),oldHsv[2]);
        else if(s.mode=="shift")target=hsv(oldHsv[0]+s.hue,std::min(1.,oldHsv[1]*s.saturation/100),oldHsv[2]);
        else if(s.mode=="gradient"||s.mode=="max_rgb"||s.mode=="gray_map"){
            double t=s.mode=="gray_map"?(.299*rgb[0]+.587*rgb[1]+.114*rgb[2])/255:maximum/255;target=gradient(colors,t);auto mapped=toHsv(target);target=hsv(mapped[0],std::min(1.,mapped[1]*s.saturation/100),mapped[2]);
        }else if(s.mode=="tint"){auto mapped=toHsv(gradient(colors,maximum/255));target=hsv(mapped[0],std::min(1.,mapped[1]*s.saturation/100),oldHsv[2]);}
        else if(s.mode=="cosine_grad"){double t=std::fmod(((double(x)/std::max(1,width-1)+double(y)/std::max(1,height-1))*.5*s.frequency)+s.offset,1.);double ease=(1-std::cos(t*6.283185307179586))*.5;target=gradient(colors,ease);for(auto& c:target)c*=oldHsv[2];}
        else if(s.mode=="glitch"){int shift=std::max(1,int(s.density/4));target={source(x+shift,y+shift,0),rgb[1],source(x-shift,y-shift,2)};alpha=std::max({alpha,source(x+shift,y+shift,3),source(x-shift,y-shift,3)});}
        else if(s.mode=="line_art"){double edge=0;for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++)edge+=source(x+dx,y+dy,0)*(dx==0&&dy==0?8:-1);target=gradient(colors,maximum/255);alpha=std::clamp(std::abs(edge)*double(rgba[i+3])/255*2,0.,255.);}
        else if(s.mode=="transparent"){target={0,0,0};alpha=0;}
        for(int c=0;c<3;c++)output[i+c]=byte(rgb[c]*(1-blend)+target[c]*s.brightness/100*blend);
        if(!s.preserveAlpha||s.mode=="transparent"||s.mode=="line_art"||s.mode=="glitch")output[i+3]=byte(rgba[i+3]*(1-blend)+alpha*blend);
    }
    if(s.overlay!="none"&&s.density>0){
        auto base=output;RGB color=colors[colors.size()/2];std::mt19937 random(s.seed);std::vector<std::pair<int,int>> valid;
        for(int y=0;y<height;y++)for(int x=0;x<width;x++)if(base[(size_t(y)*width+x)*4+3]>20)valid.emplace_back(x,y);
        auto stamp=[&](int x,int y,double alpha){if(x<0||x>=width||y<0||y>=height)return;size_t i=(size_t(y)*width+x)*4;alpha=std::clamp(alpha*blend,0.,1.);for(int c=0;c<3;c++)output[i+c]=byte(output[i+c]*(1-alpha)+color[c]*alpha);if(!s.preserveAlpha)output[i+3]=byte(std::max(double(output[i+3]),alpha*255));};
        if(s.overlay=="outer_glow"){int radius=std::clamp(int(s.scale*3),1,12);for(auto [x,y]:valid)if(x%2==0&&y%2==0)for(int dy=-radius;dy<=radius;dy++)for(int dx=-radius;dx<=radius;dx++)if(dx*dx+dy*dy<=radius*radius)stamp(x+dx,y+dy,.08*s.density/50);}
        else if(!valid.empty()){int count=std::clamp(int(valid.size()/800*s.density/25),1,300);for(int n=0;n<count;n++){auto [x,y]=valid[random()%valid.size()];int radius=std::max(2,int(4*s.scale));for(int d=-radius;d<=radius;d++){if(s.overlay=="lightning")stamp(x+d,y+d+int(random()%3)-1,.9);else if(s.overlay=="sakura"){stamp(x+d,y+int(std::sin(d)*radius/2),.65);stamp(x+int(std::sin(d)*radius/2),y+d,.65);}else {stamp(x+d,y,1-std::abs(double(d))/(radius+1));stamp(x,y+d,1-std::abs(double(d))/(radius+1));}}}}
        if(s.preserveAlpha)for(size_t i=3;i<output.size();i+=4)output[i]=rgba[i];
    }
    return output;
}
std::string imageKey(std::string value){value=rep::canonical(std::move(value));while(!value.empty()&&value.front()=='/')value.erase(value.begin());if(value.rfind("sprite/",0)!=0)value="sprite/"+value;return value;}
FrameTransform makeTransform(std::shared_ptr<const DyeSnapshot> snapshot){
    struct Cached {std::weak_ptr<rep::Pixels> source;std::shared_ptr<rep::Frame> result;};
    auto cache=std::make_shared<std::map<std::tuple<std::string,int,int>,Cached>>();
    return [snapshot=std::move(snapshot),cache](const std::string& path,int index,int palette,std::shared_ptr<rep::Frame> original){
        auto rule=snapshot->rules.find(imageKey(path));if(rule==snapshot->rules.end()||original->empty)return original;auto key=std::tuple{imageKey(path),index,palette};if(auto found=cache->find(key);found!=cache->end()&&found->second.source.lock()==original->texture)return found->second.result;
        auto cropped=original->cropped(),dyed=recolor(cropped,original->width,original->height,rule->second);if(dyed==cropped){(*cache)[key]={original->texture,original};return original;}
        auto frame=std::make_shared<rep::Frame>(*original);auto pixels=std::make_shared<rep::Pixels>();pixels->width=frame->width;pixels->height=frame->height;pixels->rgba=std::move(dyed);frame->texture=rep::nativeImageTexture(pixels);frame->rect={0,0,frame->width,frame->height};frame->rotated=frame->atlas=false;(*cache)[key]={original->texture,frame};return frame;
    };
}
}

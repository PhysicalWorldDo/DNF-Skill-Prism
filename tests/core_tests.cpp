#include "prism_core.hpp"
#include "dnflib.h"
#include <iostream>
#include <stdexcept>
#include <zlib.h>

int failures=0;
void check(bool passed,const char* message){std::cout<<(passed?"PASS ":"FAIL ")<<message<<'\n';if(!passed)failures++;}
rep::Bytes pixelLengthFixture(int format,const rep::Bytes& raw){
    rep::Bytes compressed(compressBound(uLong(raw.size())));uLongf size=compressed.size();
    if(compress2(compressed.data(),&size,raw.data(),uLong(raw.size()),Z_DEFAULT_COMPRESSION)!=Z_OK)throw std::runtime_error("fixture compression failed");
    compressed.resize(size);rep::Bytes bytes;const char magic[]="Neople Img File";bytes.insert(bytes.end(),magic,magic+16);
    auto append=[&](int32_t value){auto at=bytes.size();bytes.resize(at+4);rep::put<int32_t>(bytes,at,value);};
    for(int32_t value:{80,0,2,3,format,6,2,1,int32_t(size),-3,4,32,32,16,5,1,1,4,0,0,1,1,17,0})append(value);
    bytes.insert(bytes.end(),compressed.begin(),compressed.end());bytes.insert(bytes.end(),{255,0,0,255});return bytes;
}
int main(){
    const rep::Bytes expected{255,0,0,255,0,255,0,255};
    for(auto [format,raw]:std::vector<std::pair<int,rep::Bytes>>{{14,{0,252,224,131}},{15,{0,255,240,240}},{16,{0,0,255,255,0,255,0,255}}}){
        for(bool trailing:{false,true}){
            auto payload=raw;if(trailing)payload.insert(payload.end(),{18,52,86,120,154});
            try{rep::Img fixture(pixelLengthFixture(format,payload));auto first=fixture.frame(0);
                check(first->cropped()==expected,trailing?"direct pixels ignore decompressed suffix":"exact direct pixel payload still decodes");
                check(first->x==-3&&first->y==4&&first->fullWidth==32&&first->fullHeight==32,"pixel suffix preserves frame metadata");
                check(fixture.frame(1)->cropped()==rep::Bytes({0,0,255,255})&&fixture.frame(2)->cropped()==expected,"pixel suffix preserves subsequent payload offsets and linked frames");
            }catch(const rep::Error& error){std::cout<<error.what()<<'\n';check(false,"valid direct pixel payload decodes");}
        }
        raw.pop_back();bool truncated=false;try{rep::Img fixture(pixelLengthFixture(format,raw));fixture.frame(0);}catch(const rep::Error&){truncated=true;}
        check(truncated,"short decompressed direct pixel payload is rejected");
    }
    prism::ColorSettings settings;settings.mode="hue";settings.hue=120;settings.saturation=100;settings.intensity=100;
    rep::Bytes pixels{255,0,0,128,0,0,0,0,255,255,255,230};
    auto result=prism::recolor(pixels,3,1,settings);
    check(result[1]>250&&result[0]<4&&result[2]<4,"target hue changes red to green");
    check(result[3]==128&&result[7]==0&&result[11]==230,"pure recolor preserves alpha");
    check(result[4]==0&&result[5]==0&&result[6]==0,"black remains black");
    check(result[8]==255&&result[9]==255&&result[10]==255,"white highlight remains white");
    settings.intensity=0;check(prism::recolor(pixels,3,1,settings)==pixels,"zero intensity is exact identity");
    settings.mode="gradient";settings.intensity=100;settings.preserveHighlight=false;settings.colors={"#000000","#00ff00","#ffffff"};
    auto gradient=prism::recolor(rep::Bytes{128,128,128,255},1,1,settings);
    check(gradient[1]>250&&gradient[0]<5,"gradient maps middle value to middle stop");
    bool rejected=false;try{prism::colorSettings(prism::Json{{"hue","bad"}});}catch(...){rejected=true;}
    check(rejected,"invalid color settings are rejected");
    check(prism::imageKey("/Character\\Test.IMG")=="sprite/character/test.img","resource keys normalize identically to player");
    ImgFile* img=nullptr;dnf_img_create(&img);
    rep::Bytes frame{255,0,0,128,255,0,0,255,255,0,0,255,255,0,0,255};
    dnf_img_add_frame_rgba(img,frame.data(),2,2,-3,4,32,32,DNF_FMT_ARGB_8888,DNF_COMPRESS_ZLIB);
    dnf_img_add_link_frame(img,0);
    uint8_t* raw=nullptr;size_t length=0;dnf_img_save_mem(img,&raw,&length);rep::Bytes source(raw,raw+length);dnf_free(raw);dnf_img_free(img);
    settings.mode="hue";settings.hue=120;settings.intensity=100;settings.preserveHighlight=true;
    auto baked=prism::bakeImg(source,settings);
    rep::Img reopened(baked.bytes);auto bakedFrame=reopened.frame(0);
    check(baked.changed&&bakedFrame->cropped()[1]>250,"dnflib output reopens with dyed pixels in REP reader");
    check(bakedFrame->x==-3&&bakedFrame->y==4&&bakedFrame->fullWidth==32,"IMG write preserves offsets and canvas");
    check(reopened.count()==2&&reopened.frame(1)->cropped()==bakedFrame->cropped(),"IMG write preserves frame ordinals and links");
    settings.intensity=0;check(prism::bakeImg(source,settings).bytes==source,"unchanged IMG bytes are preserved");
    prism::Skill skill;skill.english="SeaDragonTooth";skill.family="帝国骑士";skill.type="vp1";
    rep::ui::SkillItem right,wrong;right.english="SeaDragonTooth_VP1";right.job="ImperialKnight";right.jobZh="帝国骑士";wrong.english="SeaDragonTooth_VP2";wrong.job=right.job;wrong.jobZh=right.jobZh;
    check(prism::matchReplays(skill,{right,wrong}).size()==1,"REP association respects VP variant");
    skill.english="seadragontooth_vp1";
    check(prism::matchReplays(skill,{right,wrong}).size()==1,"database VP suffix is not appended twice");
    auto rules=std::make_shared<prism::DyeSnapshot>();rules->rules["sprite/test.img"]=settings;rules->rules.begin()->second.intensity=100;
    auto transform=prism::makeTransform(rules);
    auto first=std::make_shared<rep::Frame>(*bakedFrame),second=std::make_shared<rep::Frame>(*bakedFrame);
    first->texture=std::make_shared<rep::Pixels>(*bakedFrame->texture);second->texture=std::make_shared<rep::Pixels>(*bakedFrame->texture);
    std::fill(second->texture->rgba.begin(),second->texture->rgba.end(),0);
    auto firstResult=transform("sprite/test.img",0,0,first),secondResult=transform("sprite/test.img",0,0,second);
    check(firstResult->cropped()!=secondResult->cropped(),"same rule reloads pixels when source texture changes");
    std::cout<<"FAILURES "<<failures<<'\n';return failures?1:0;
}

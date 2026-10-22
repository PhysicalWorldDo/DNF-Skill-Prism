#include "prism_core.hpp"
#include "dnflib.h"
#include <iostream>
#include <chrono>
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
rep::Bytes multiPaletteFixture(int emptyPalette=-1){
    rep::Bytes bytes;const char magic[]="Neople Img File";bytes.insert(bytes.end(),magic,magic+16);
    auto word=[&](int32_t v){auto n=bytes.size();bytes.resize(n+4);rep::put(bytes,n,v);};
    for(int v:{88,0,6,4,15})word(v);
    for(int p=0;p<15;p++){if(p==emptyPalette){word(0);continue;}word(3);bytes.insert(bytes.end(),{0,0,0,0,uint8_t(255-p*9),uint8_t(p*7),uint8_t(p*11),128,20,170,90,255});}
    rep::Bytes indices{1,2,0,2,1,0},compressed(compressBound(indices.size()));uLongf length=compressed.size();compress2(compressed.data(),&length,indices.data(),indices.size(),6);compressed.resize(length);
    word(17);word(1);
    for(int v:{14,6,3,2,int(compressed.size()),-3,4,32,32})word(v);
    for(int v:{16,5,2,1,8,1,-2,16,16})word(v);
    word(17);word(1);
    bytes.insert(bytes.end(),compressed.begin(),compressed.end());bytes.insert(bytes.end(),{0,0,255,128,0,255,0,255});return bytes;
}
rep::Bytes paletteCapacityFixture(int width){
    rep::Bytes bytes;const char magic[]="Neople Img File";bytes.insert(bytes.end(),magic,magic+16);
    auto word=[&](int32_t v){auto n=bytes.size();bytes.resize(n+4);rep::put(bytes,n,v);};
    for(int v:{36,0,6,1,2})word(v);
    for(int p=0;p<2;p++){word(256);for(int c=0;c<256;c++)bytes.insert(bytes.end(),{uint8_t(p?0:c),uint8_t(p?255-c:0),0,uint8_t(p?128:255)});}
    rep::Bytes indices(width*2);for(size_t i=0;i<indices.size();i++)indices[i]=uint8_t(i);rep::Bytes compressed(compressBound(indices.size()));uLongf size=compressed.size();compress2(compressed.data(),&size,indices.data(),indices.size(),6);compressed.resize(size);
    for(int v:{14,6,width,2,int(size),0,0,width,2})word(v);bytes.insert(bytes.end(),compressed.begin(),compressed.end());return bytes;
}
void multiPaletteTests(){
    auto bytes=multiPaletteFixture();rep::Img original(bytes);
    prism::ColorSettings capacity;capacity.mode="hue";capacity.hue=120;capacity.intensity=100;
    try{auto maximum=prism::bakeImg(paletteCapacityFixture(256),capacity);rep::Img reopened(maximum.bytes);check(rep::at<int32_t>(maximum.bytes,36)==256&&reopened.paletteCount()==2,"V6 retains the full 256-color index capacity");}catch(...){check(false,"V6 retains the full 256-color index capacity");}
    capacity.mode="cosine_grad";capacity.colors={"#000000","#ffffff"};capacity.frequency=.37;capacity.offset=.13;
    bool overflow=false;try{prism::bakeImg(paletteCapacityFixture(4096),capacity);}catch(const rep::Error& e){overflow=std::string(e.what()).find("256")!=std::string::npos;}check(overflow,"a 257th palette color tuple is rejected without lossy export");

    for(auto mode:{"hue","gradient","cosine_grad","line_art","glitch","transparent"}){
        prism::ColorSettings settings;settings.mode=mode;settings.hue=120;settings.intensity=100;settings.preserveHighlight=false;
        try{
            auto baked=prism::bakeImg(bytes,settings);rep::Img result(baked.bytes);
            check(result.version==6&&result.paletteCount()==15&&result.count()==4,"multi-palette bake retains v6, all 15 palettes and frame ordinals");
            check(result.linkIndex(0)==1&&result.linkIndex(3)==1,"multi-palette bake preserves forward and backward links");
            bool equal=true;
            ImgFile* decoded=nullptr;dnf_img_open_mem(baked.bytes.data(),baked.bytes.size(),&decoded);
            for(int p=0;p<15;p++)for(int i=0;i<4;i++){
                auto before=original.frame(i,p),after=result.frame(i,p);auto expected=prism::recolor(before->cropped(),before->width,before->height,settings);
                equal&=after->cropped()==expected&&after->x==before->x&&after->y==before->y&&after->fullWidth==before->fullWidth&&after->fullHeight==before->fullHeight;
                uint8_t* pixels=nullptr;int w=0,h=0;auto status=dnf_image_decode_with_palette(decoded,i,p,&pixels,&w,&h);
                equal&=status==DNF_OK&&w==before->width&&h==before->height&&pixels&&std::equal(expected.begin(),expected.end(),pixels);dnf_free(pixels);
            }
            dnf_img_free(decoded);check(equal,"all palettes and linked/direct frames match preview and independent dnflib decode");
        }catch(const std::exception& e){std::cout<<e.what()<<'\n';check(false,"multi-palette IMG exports rather than rejecting the entire resource");}
    }
    prism::ColorSettings unchanged;unchanged.intensity=0;try{check(prism::bakeImg(bytes,unchanged).bytes==bytes,"unchanged multi-palette bytes remain exact");}catch(...){check(false,"unchanged multi-palette bytes remain exact");}
}
void aliasExportTests(){
    auto folder=std::filesystem::current_path()/"validation"/("alias_export_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto packs=folder/"client"/"ImagePacks2";std::filesystem::create_directories(packs);
    const std::string base="sprite/character/swordman/equipment/weapon/katana/katana0100b.img",alias="sprite/character/swordman/equipment/weapon/katana/katana0111b.img",ui="sprite/interface/panel.img",hd="sprite/interface/(2x)panel.img";
    auto bytes=multiPaletteFixture(),uiBytes=pixelLengthFixture(16,{0,0,255,255,0,0,255,128});
    NpkFile* package=nullptr;dnf_npk_create(&package);dnf_npk_add_entry(package,base.c_str(),bytes.data(),int(bytes.size()));dnf_npk_add_entry(package,hd.c_str(),uiBytes.data(),int(uiBytes.size()));dnf_npk_save_file(rep::utf8((packs/"!fixture.npk").wstring()).c_str(),package,0);dnf_npk_free(package);std::filesystem::copy_file(packs/"!fixture.npk",packs/"sprite_character_swordman_equipment_weapon_katana.NPK");std::filesystem::copy_file(packs/"!fixture.npk",packs/"sprite_interface.NPK");
    std::filesystem::create_directories(folder/"output");std::filesystem::copy_file(packs/"!fixture.npk",folder/"output"/"sprite_boundary.npk");
    try{
        rep::Assets source(packs);check(source.frame(alias,1,0,false)->geometryScale==1&&source.frame(ui,0,0,false)->geometryScale==.5f,"alias export fixture resolves weapon palette and 2x preview before export");prism::DyeSnapshot recipe;prism::ColorSettings color;color.mode="hue";color.hue=120;color.intensity=100;recipe.rules[alias]=recipe.rules[ui]=recipe.rules[hd]=color;
        auto result=prism::writePatch(source,recipe,folder/"output"/"!dyed.npk");rep::Assets output(folder/"output");
        auto weapon=output.frame(alias,1,0,false);rep::Img original(bytes);auto before=original.frame(1,11);
        check(weapon->cropped()==prism::recolor(before->cropped(),before->width,before->height,color),"missing numeric weapon export bakes selected palette11 under its exact legacy path");
        check(output.preload(alias)->paletteCount()==0,"legacy weapon alias export does not select palette0 of the base IMG");
        check(output.preload(hd)&&!output.preload(ui)&&output.frame(ui,0,0,false)->geometryScale==.5f,"2x alias export keeps the physical 2x path and logical half geometry");
        check(result.entries==2,"aliases with the same physical source and recipe are deduplicated");
        recipe.rules[hd].hue=240;bool conflict=false;auto conflictFile=folder/"output"/"!conflict.npk";try{prism::writePatch(source,recipe,conflictFile);}catch(const rep::Error&){conflict=true;}check(conflict&&!std::filesystem::exists(conflictFile),"conflicting physical 2x recipes fail before a patch is written");
        const auto prior=recipe.rules;auto originals=prism::undyedSnapshot({base,alias,ui,hd});
        auto originalPatch=prism::writePatch(source,originals,folder/"originals"/"!original.npk",false);
        std::filesystem::copy_file(packs/"!fixture.npk",folder/"originals"/"sprite_boundary.npk");rep::Assets uncolored(folder/"originals");
        check(originalPatch.entries==3&&uncolored.rawImage(base)==bytes&&uncolored.rawImage(hd)==uiBytes,"undyed NPK includes unmodified selections and retains exact direct and multi-palette IMG bytes");
        check(uncolored.frame(alias,1,0,false)->cropped()==before->cropped(),"undyed NPK materializes the selected weapon palette without recoloring");
        check(recipe.rules==prior,"exporting an undyed snapshot leaves the existing dyed recipe intact");
        bool empty=false;try{prism::writePatch(source,originals,folder/"empty.npk");}catch(const rep::Error&){empty=true;}
        check(empty&&!std::filesystem::exists(folder/"empty.npk"),"modified-only export still omits unchanged originals");
        auto broken=multiPaletteFixture(11);dnf_npk_create(&package);dnf_npk_add_entry(package,base.c_str(),broken.data(),int(broken.size()));dnf_npk_save_file(rep::utf8((packs/"!bad.npk").wstring()).c_str(),package,0);dnf_npk_free(package);
        rep::Assets badSource(packs),shared(badSource.index());auto validAlias=badSource.exportSource("sprite/character/swordman/equipment/weapon/katana/katana0101b.img");
        check(validAlias.palette==1&&badSource.sourceRevision()==0,"selected alias export validates only the required palette, keeping valid patches");
        auto all=badSource.exportSource(base);rep::Img recovered(all.bytes);
        check(recovered.frame(1,11)->cropped()==before->cropped()&&badSource.sourceRevision()==1&&shared.frame(base,1,11,false)->cropped()==before->cropped(),"full multi-palette export rejects bad later palette and shares original fallback");

    }catch(const std::exception& e){std::cout<<e.what()<<'\n';check(false,"selected compatibility aliases export successfully");}
}
int main(){
    {
        const rep::Bytes pixels{80,140,210,128,0,0,0,0,255,255,255,230,10,30,60,255};
        for(auto overlay:{"none","outer_glow","lightning","starlight","sakura"}){
            prism::ColorSettings original;original.mode="none";original.overlay=overlay;original.brightness=170;original.saturation=190;original.intensity=100;original.preserveAlpha=false;
            check(prism::recolor(pixels,2,2,original)==pixels,"original pixel mode bypasses brightness, overlays and alpha edits");
            auto bytes=multiPaletteFixture();auto baked=prism::bakeImg(bytes,original);
            check(!baked.changed&&baked.bytes==bytes,"original mode retains exact multi-palette IMG bytes with stale overlays");
            auto alias=prism::bakeImg(bytes,original,11);rep::Img source(bytes),out(alias.bytes);
            check(out.frame(1)->cropped()==source.frame(1,11)->cropped(),"original weapon alias retains its actual palette pixels");
        }
    }
    multiPaletteTests();
    aliasExportTests();
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
    first->geometryScale=.5f;
    first->texture=std::make_shared<rep::Pixels>(*bakedFrame->texture);second->texture=std::make_shared<rep::Pixels>(*bakedFrame->texture);
    std::fill(second->texture->rgba.begin(),second->texture->rgba.end(),0);
    auto firstResult=transform("sprite/test.img",0,0,first),secondResult=transform("sprite/test.img",0,0,second);
    check(firstResult->cropped()!=secondResult->cropped(),"same rule reloads pixels when source texture changes");
    check(firstResult->geometryScale==.5f&&firstResult->width==first->width&&firstResult->height==first->height&&firstResult->x==first->x&&firstResult->y==first->y,"recolor preserves 2x alias density, physical pixels and IMG offsets");
    check(secondResult->geometryScale==1&&bakedFrame->geometryScale==1,"recolor keeps original 1x geometry independent of 2x alias");
    std::cout<<"FAILURES "<<failures<<'\n';return failures?1:0;
}

#include "prism_core.hpp"
#include "dnflib.h"
#include <algorithm>

namespace prism {
static void nativeCheck(DnfError error,const char* operation){if(error!=DNF_OK)throw rep::Error(std::string(operation)+" failed (dnflib "+std::to_string(error)+")");}
static BakeResult bakePalettes(rep::Img& original,std::span<const uint8_t> bytes,const ColorSettings& settings){
    // Each byte index names a tuple of colors across all palettes. Re-index
    // exact tuples so spatial recipes stay identical to every palette preview.
    struct BakedFrame {std::shared_ptr<rep::Frame> frame;rep::Bytes pixels,indices;std::optional<int> link;};
    std::vector<BakedFrame> frames;std::vector<rep::Bytes> palettes(original.paletteCount());std::map<rep::Bytes,uint8_t> colors;bool changed=false;
    for(size_t i=0;i<original.count();i++){
        BakedFrame frame;frame.frame=original.frame(int(i));frame.link=original.linkIndex(int(i));
        if(!frame.link){
            std::vector<rep::Bytes> variants;
            for(size_t p=0;p<palettes.size();p++){auto source=original.frame(int(i),int(p));auto before=source->cropped();auto after=source->empty?before:recolor(before,source->width,source->height,settings);changed|=before!=after;variants.push_back(std::move(after));}
            bool direct=std::all_of(variants.begin()+1,variants.end(),[&](const auto& pixels){return pixels==variants.front();});
            if(direct)frame.pixels=std::move(variants.front());
            else for(size_t offset=0;offset<variants.front().size();offset+=4){
                rep::Bytes key;key.reserve(palettes.size()*4);for(const auto& pixels:variants)key.insert(key.end(),pixels.begin()+offset,pixels.begin()+offset+4);
                auto found=colors.find(key);
                if(found==colors.end()){
                    if(colors.size()==256)throw rep::Error("染色结果超过 V6 索引可无损保存的 256 组颜色，请调整空间特效参数");
                    auto index=uint8_t(colors.size());found=colors.emplace(std::move(key),index).first;
                    for(size_t p=0;p<palettes.size();p++)palettes[p].insert(palettes[p].end(),found->first.begin()+p*4,found->first.begin()+p*4+4);
                }
                frame.indices.push_back(found->second);
            }
        }
        frames.push_back(std::move(frame));
    }
    if(!changed)return {{bytes.begin(),bytes.end()},false,original.version};
    rep::Bytes paletteBytes;for(auto& palette:palettes){if(palette.empty())palette.resize(4);paletteBytes.insert(paletteBytes.end(),palette.begin(),palette.end());}
    ImgFile* raw=nullptr;nativeCheck(dnf_img_create_paletted(paletteBytes.data(),int(palettes.size()),int(std::max<size_t>(1,colors.size())),&raw),"Create V6 IMG");std::unique_ptr<ImgFile,decltype(&dnf_img_free)> destination(raw,dnf_img_free);
    for(auto& frame:frames){auto& f=*frame.frame;
        if(frame.link)frame.pixels=f.cropped();
        if(frame.indices.empty())nativeCheck(dnf_img_add_frame_rgba(raw,frame.pixels.data(),f.width,f.height,f.x,f.y,std::max(1,f.fullWidth),std::max(1,f.fullHeight),DNF_FMT_ARGB_8888,DNF_COMPRESS_ZLIB),"Append direct V6 frame");
        else nativeCheck(dnf_img_add_frame_indices(raw,frame.indices.data(),f.width,f.height,f.x,f.y,std::max(1,f.fullWidth),std::max(1,f.fullHeight)),"Append indexed V6 frame");
    }
    for(size_t i=0;i<frames.size();i++)if(frames[i].link){auto& f=raw->frames[i];dnf_free(f.data);f.data=nullptr;f.data_size=0;f.format=DNF_FMT_LINK;f.link_index=*frames[i].link;}
    uint8_t* encoded=nullptr;size_t size=0;nativeCheck(dnf_img_save_mem(raw,&encoded,&size),"Serialize V6 IMG");std::unique_ptr<uint8_t,decltype(&dnf_free)> owned(encoded,dnf_free);return {{encoded,encoded+size},true,original.version};
}
BakeResult bakeImg(std::span<const uint8_t> bytes,const ColorSettings& settings,std::optional<int> palette){
    rep::Img original(rep::Bytes(bytes.begin(),bytes.end()));
    if(!palette&&original.paletteCount()>1)return bakePalettes(original,bytes,settings);
    ImgFile* raw=nullptr;nativeCheck(dnf_img_create(&raw),"Create IMG");std::unique_ptr<ImgFile,decltype(&dnf_img_free)> destination(raw,dnf_img_free);
    bool changed=false;std::vector<std::pair<int,int>> links;
    for(size_t i=0;i<original.count();i++){
        auto frame=original.frame(int(i),palette.value_or(0));auto pixels=frame->cropped();auto dyed=frame->empty?pixels:recolor(pixels,frame->width,frame->height,settings);changed|=pixels!=dyed;
        nativeCheck(dnf_img_add_frame_rgba(destination.get(),dyed.data(),frame->width,frame->height,frame->x,frame->y,std::max(1,frame->fullWidth),std::max(1,frame->fullHeight),DNF_FMT_ARGB_8888,DNF_COMPRESS_ZLIB),"Append IMG frame");
        if(auto link=original.linkIndex(int(i)))links.emplace_back(int(i),*link);
    }
    if(!changed&&!palette)return {{bytes.begin(),bytes.end()},false,original.version};
    for(auto [index,target]:links){auto& frame=destination->frames[index];dnf_free(frame.data);frame.data=nullptr;frame.data_size=0;frame.format=DNF_FMT_LINK;frame.link_index=target;}
    uint8_t* encoded=nullptr;size_t size=0;nativeCheck(dnf_img_save_mem(destination.get(),&encoded,&size),"Serialize IMG");std::unique_ptr<uint8_t,decltype(&dnf_free)> owned(encoded,dnf_free);return {{encoded,encoded+size},changed,original.version};
}
PatchResult writePatch(rep::Assets& assets,const DyeSnapshot& snapshot,const std::filesystem::path& output,bool modifiedOnly,const std::function<bool()>& cancelled,const std::function<void(size_t,size_t)>& progress){
    if(snapshot.rules.empty())throw rep::Error("请选择需要染色的 IMG 素材");if(std::filesystem::exists(output))throw rep::Error("导出文件已存在，请使用新文件名");
    auto destination=std::filesystem::absolute(output).lexically_normal(),client=std::filesystem::absolute(assets.root().parent_path()).lexically_normal();auto relative=destination.lexically_relative(client);if(!relative.empty()&&*relative.begin()!=L"..")throw rep::Error("请导出到客户端目录之外，原始客户端只读");
    NpkFile* raw=nullptr;nativeCheck(dnf_npk_create(&raw),"Create NPK");std::unique_ptr<NpkFile,decltype(&dnf_npk_free)> npk(raw,dnf_npk_free);PatchResult result;result.path=destination;
    std::map<std::string,ColorSettings> emitted;size_t processed=0;for(auto& [path,settings]:snapshot.rules){if(cancelled&&cancelled())throw rep::Error("导出已取消");auto source=assets.exportSource(path);if(auto prior=emitted.find(source.entryPath);prior!=emitted.end()){if(prior->second!=settings)throw rep::Error("同一实际 IMG 的染色参数冲突："+source.entryPath);if(progress)progress(++processed,snapshot.rules.size());continue;}emitted[source.entryPath]=settings;auto baked=bakeImg(source.bytes,settings,source.palette);if(baked.changed||!modifiedOnly){nativeCheck(dnf_npk_add_entry(npk.get(),source.entryPath.c_str(),baked.bytes.data(),int32_t(baked.bytes.size())),"Append NPK entry");result.paths.push_back(source.entryPath);result.entries++;}if(progress)progress(++processed,snapshot.rules.size());}
    if(!result.entries)throw rep::Error("当前参数没有改变任何像素，未生成空补丁");
    std::filesystem::create_directories(destination.parent_path());auto temporary=destination;temporary+=L".partial";
    if(std::filesystem::exists(temporary))throw rep::Error("临时导出文件已存在，请更换文件名");
    try{nativeCheck(dnf_npk_save_file(rep::utf8(temporary.wstring()).c_str(),npk.get(),0),"Save NPK");if(cancelled&&cancelled())throw rep::Error("导出已取消");std::filesystem::rename(temporary,destination);result.bytes=std::filesystem::file_size(destination);}catch(...){std::error_code error;std::filesystem::remove(temporary,error);throw;}return result;
}
}

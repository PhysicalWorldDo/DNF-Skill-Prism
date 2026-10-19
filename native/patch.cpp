#include "prism_core.hpp"
#include "dnflib.h"
#include <algorithm>

namespace prism {
static void nativeCheck(DnfError error,const char* operation){if(error!=DNF_OK)throw rep::Error(std::string(operation)+" failed (dnflib "+std::to_string(error)+")");}
BakeResult bakeImg(std::span<const uint8_t> bytes,const ColorSettings& settings){
    rep::Img original(rep::Bytes(bytes.begin(),bytes.end()));if(original.paletteCount()>1)throw rep::Error("多调色板 IMG 暂不支持导出，未写入补丁");
    ImgFile* raw=nullptr;nativeCheck(dnf_img_create(&raw),"Create IMG");std::unique_ptr<ImgFile,decltype(&dnf_img_free)> destination(raw,dnf_img_free);
    bool changed=false;std::vector<std::pair<int,int>> links;
    for(size_t i=0;i<original.count();i++){
        auto frame=original.frame(int(i));auto pixels=frame->cropped();auto dyed=frame->empty?pixels:recolor(pixels,frame->width,frame->height,settings);changed|=pixels!=dyed;
        nativeCheck(dnf_img_add_frame_rgba(destination.get(),dyed.data(),frame->width,frame->height,frame->x,frame->y,std::max(1,frame->fullWidth),std::max(1,frame->fullHeight),DNF_FMT_ARGB_8888,DNF_COMPRESS_ZLIB),"Append IMG frame");
        if(auto link=original.linkIndex(int(i)))links.emplace_back(int(i),*link);
    }
    if(!changed)return {{bytes.begin(),bytes.end()},false,original.version};
    // Fill links in a second pass to retain legal forward references and ordinals.
    for(auto [index,target]:links){auto& frame=destination->frames[index];dnf_free(frame.data);frame.data=nullptr;frame.data_size=0;frame.format=DNF_FMT_LINK;frame.link_index=target;}
    uint8_t* encoded=nullptr;size_t size=0;nativeCheck(dnf_img_save_mem(destination.get(),&encoded,&size),"Serialize IMG");std::unique_ptr<uint8_t,decltype(&dnf_free)> owned(encoded,dnf_free);return {{encoded,encoded+size},true,original.version};
}
PatchResult writePatch(rep::Assets& assets,const DyeSnapshot& snapshot,const std::filesystem::path& output,bool modifiedOnly,const std::function<bool()>& cancelled,const std::function<void(size_t,size_t)>& progress){
    if(snapshot.rules.empty())throw rep::Error("请选择需要染色的 IMG 素材");if(std::filesystem::exists(output))throw rep::Error("导出文件已存在，请使用新文件名");
    auto destination=std::filesystem::absolute(output).lexically_normal(),client=std::filesystem::absolute(assets.root().parent_path()).lexically_normal();auto relative=destination.lexically_relative(client);if(!relative.empty()&&*relative.begin()!=L"..")throw rep::Error("请导出到客户端目录之外，原始客户端只读");
    NpkFile* raw=nullptr;nativeCheck(dnf_npk_create(&raw),"Create NPK");std::unique_ptr<NpkFile,decltype(&dnf_npk_free)> npk(raw,dnf_npk_free);PatchResult result;result.path=destination;
    size_t processed=0;for(auto& [path,settings]:snapshot.rules){if(cancelled&&cancelled())throw rep::Error("导出已取消");auto source=assets.rawImage(path);auto baked=bakeImg(source,settings);if(baked.changed||!modifiedOnly){nativeCheck(dnf_npk_add_entry(npk.get(),path.c_str(),baked.bytes.data(),int32_t(baked.bytes.size())),"Append NPK entry");result.paths.push_back(path);result.entries++;}if(progress)progress(++processed,snapshot.rules.size());}
    if(!result.entries)throw rep::Error("当前参数没有改变任何像素，未生成空补丁");
    std::filesystem::create_directories(destination.parent_path());auto temporary=destination;temporary+=L".partial";
    if(std::filesystem::exists(temporary))throw rep::Error("临时导出文件已存在，请更换文件名");
    try{nativeCheck(dnf_npk_save_file(rep::utf8(temporary.wstring()).c_str(),npk.get(),0),"Save NPK");if(cancelled&&cancelled())throw rep::Error("导出已取消");std::filesystem::rename(temporary,destination);result.bytes=std::filesystem::file_size(destination);}catch(...){std::error_code error;std::filesystem::remove(temporary,error);throw;}return result;
}
}

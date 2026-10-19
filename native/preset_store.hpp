#pragma once
#include "prism_core.hpp"
#include <windows.h>
#include <fstream>
#include <set>

namespace prism {
inline Json checkedUserPresets(const Json& values){
    if(!values.is_array())throw rep::Error("预设库格式不正确");
    Json result=Json::array();std::set<std::string> ids;
    for(const auto& value:values){
        auto id=value.at("id").get<std::string>(),name=value.at("name").get<std::string>();
        if(id.rfind("user-",0)!=0||!ids.insert(id).second)throw rep::Error("预设 ID 无效或重复");
        auto wide=rep::wide(name);if(wide.empty()||wide.size()>60||wide.find_first_not_of(L" \t\r\n")==std::wstring::npos)throw rep::Error("预设名称需要 1～60 个字符");
        result.push_back({{"id",id},{"name",name},{"settings",colorJson(colorSettings(value.at("settings")))}});
    }
    return result;
}
inline Json loadUserPresets(const std::filesystem::path& file){
    if(!std::filesystem::exists(file))return Json::array();
    std::ifstream stream(file,std::ios::binary);if(!stream)throw rep::Error("无法读取用户预设库");
    Json document;stream>>document;
    if(document.value("format","")!="dnf-skill-prism-presets"||document.value("version",0)!=1)throw rep::Error("不支持的预设库格式");
    return checkedUserPresets(document.at("presets"));
}
inline void saveUserPresets(const std::filesystem::path& file,const Json& values){
    auto presets=checkedUserPresets(values);std::filesystem::create_directories(file.parent_path());
    auto temporary=file;temporary+=L".tmp";
    {
        std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
        stream<<Json{{"format","dnf-skill-prism-presets"},{"version",1},{"presets",presets}}.dump(2);
        stream.flush();if(!stream)throw rep::Error("预设保存失败，请检查目录写入权限");
    }
    if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw rep::Error("无法替换用户预设库文件");
}
}

#include "prism_core.hpp"
#include <iostream>
#include <fstream>
#if __has_include("preset_store.hpp")
#include "preset_store.hpp"
#endif

int main(){
#if __has_include("preset_store.hpp")
    auto folder=std::filesystem::temp_directory_path()/("prism-presets-"+std::to_string(GetCurrentProcessId()));
    auto file=folder/L"user-presets.json";
    std::filesystem::create_directories(folder);
    try{
        if(!prism::loadUserPresets(file).empty())throw std::runtime_error("a missing library must start empty");
        prism::ColorSettings settings;settings.mode="cosine_grad";settings.scale=2.3;settings.frequency=4.7;settings.offset=.42;settings.seed=31;settings.overlay="sakura";
        prism::Json presets=prism::Json::array({{{"id","user-test"},{"name","我的幻彩"},{"settings",prism::colorJson(settings)}}});
        prism::saveUserPresets(file,presets);
        if(prism::loadUserPresets(file)!=presets)throw std::runtime_error("reopening must restore every parameter");
        auto invalid=presets;invalid[0]["settings"]["hue"]=999;
        bool rejected=false;try{prism::saveUserPresets(file,invalid);}catch(...){rejected=true;}
        if(!rejected||prism::loadUserPresets(file)!=presets)throw std::runtime_error("invalid writes must preserve the previous library");
        invalid=presets;invalid.push_back(presets[0]);rejected=false;try{prism::saveUserPresets(file,invalid);}catch(...){rejected=true;}
        if(!rejected)throw std::runtime_error("duplicate IDs must be rejected");
        prism::saveUserPresets(file,prism::Json::array());
        if(!prism::loadUserPresets(file).empty())throw std::runtime_error("deletion must survive reopening");
        std::filesystem::remove(file);std::filesystem::remove(folder);
        std::cout<<"PASS native preset disk persistence, complete settings, deletion and failed-write preservation\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
#else
    std::cerr<<"FAIL native persistent preset store is missing\n";return 1;
#endif
}

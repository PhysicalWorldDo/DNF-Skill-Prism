#include "prism_core.hpp"
namespace prism {
DyeSnapshot undyedSnapshot(const std::vector<std::string>& selected){
    DyeSnapshot result;ColorSettings original;original.mode="none";
    for(const auto& path:selected)result.rules.emplace(imageKey(path),original);
    return result;
}
}

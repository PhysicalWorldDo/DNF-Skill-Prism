#include "prism_core.hpp"
namespace prism {
std::vector<rep::ui::SkillItem> matchReplays(const Skill& skill,const std::vector<rep::ui::SkillItem>& catalog){
    auto lower=[](std::string text){for(auto& c:text)if(c>='A'&&c<='Z')c+=32;return text;};
    std::vector<rep::ui::SkillItem> found;auto english=lower(skill.english),type=lower(skill.type);std::string suffix=type.find("vp1")!=std::string::npos?"_vp1":type.find("vp2")!=std::string::npos?"_vp2":"";
    if(english.ends_with("_vp1")||english.ends_with("_vp2")){if(suffix.empty())suffix=english.substr(english.size()-4);english.resize(english.size()-4);}
    for(auto& item:catalog){auto stem=lower(rep::utf8(item.path.stem().wstring()));if(stem.empty())stem=lower(item.english);if(stem!=english+suffix)continue;if(!skill.family.empty()&&!item.jobZh.empty()&&item.jobZh!=skill.family)continue;found.push_back(item);}return found;
}
}

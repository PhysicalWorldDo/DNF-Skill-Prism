#pragma once
#include "rep/resources.hpp"
#include "rep/catalog.hpp"
#include <nlohmann/json.hpp>
#include <map>
#include <functional>

namespace prism {
using Json=nlohmann::json;
struct ColorSettings {
    std::string mode="gradient";
    std::vector<std::string> colors{"#34205f","#9863eb","#e4d1ff"};
    double hue=276,saturation=110,brightness=100,intensity=85;
    bool preserveAlpha=true,preserveBlack=true,preserveHighlight=true;
    std::string overlay="none";
    double density=30,scale=1,frequency=1,offset=0;
    uint32_t seed=1;
    bool operator==(const ColorSettings&)const=default;
};
ColorSettings colorSettings(const Json& value);
Json colorJson(const ColorSettings& settings);
rep::Bytes recolor(std::span<const uint8_t> rgba,int width,int height,const ColorSettings& settings);
struct DyeSnapshot {uint64_t revision=0;std::map<std::string,ColorSettings> rules;};
std::string imageKey(std::string value);
using FrameTransform=std::function<std::shared_ptr<rep::Frame>(const std::string&,int,int,std::shared_ptr<rep::Frame>)>;
FrameTransform makeTransform(std::shared_ptr<const DyeSnapshot> snapshot);
struct BakeResult {rep::Bytes bytes;bool changed=false;int sourceVersion=0;};
BakeResult bakeImg(std::span<const uint8_t> bytes,const ColorSettings& settings);
struct PatchResult {std::filesystem::path path;uint64_t entries=0,bytes=0;std::vector<std::string> paths;};
PatchResult writePatch(rep::Assets& assets,const DyeSnapshot& snapshot,const std::filesystem::path& output,bool modifiedOnly=true,const std::function<bool()>& cancelled={},const std::function<void(size_t,size_t)>& progress={});
struct Skill {
    int sequence=0,iconFrame=0;
    std::string key,family,profession,name,english,type,iconPath;
};
class SkillDatabase {
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    explicit SkillDatabase(const std::filesystem::path& dll);
    ~SkillDatabase();
    std::vector<std::pair<std::string,std::string>> professions()const;
    std::vector<Skill> skills(const std::string& family,const std::string& profession)const;
    std::string version()const;
};
std::vector<rep::ui::SkillItem> matchReplays(const Skill& skill,const std::vector<rep::ui::SkillItem>& catalog);
}

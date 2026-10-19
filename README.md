# 技能染色工坊 · DNF Skill Prism

作者：**物理世界的欺骗** · [哔哩哔哩](https://space.bilibili.com/492488982)

[下载 Windows 成品](https://github.com/PhysicalWorldDo/DNF-Skill-Prism/releases/latest) · [版本 1.1.1](https://github.com/PhysicalWorldDo/DNF-Skill-Prism/releases/tag/v1.1.1)

## 成品

技能原始效果与染色效果按同一条 REP 时间线同步预览。支持颜色、渐变与叠加调整，106 套内置配色、自定义配色永久保存、跨技能复用、方案保存与 NPK 补丁导出。

1.1.1 成品增加首次使用交互教程，以及当前转职内的技能批量配色、一次撤销和合并 NPK 导出。批量分析只包含技能实际存在的原版或 VP 版本，不再为本来没有的 VP 生成提示行。

下载 Release 中的 `DNFSkillPrism-v1.1.1-win-x64.zip`，完整解压后运行 `dnf_skill_prism.exe`。要求 Windows 10/11 x64、Microsoft Edge WebView2 Runtime，以及自行准备的游戏客户端。不要只复制 EXE，`resources` 是运行依赖。

原始客户端只读。共享素材修改会影响所有使用它的技能。多调色板 IMG 目前支持预览，暂不支持补丁导出。

## 仓库范围

本仓库仅公开原生核心源码：染色算法与参数序列化、不可变方案快照、IMG 读取与烘焙、NPK 补丁写入、REP 协议与资源读取、技能录像匹配、自定义预设持久化，以及核心回归测试。

**整套桌面界面实现不公开。** 仓库不包含 HTML/CSS/JavaScript、界面皮肤与素材、WebView 宿主、桌面窗口、教程和批量操作的桌面集成、客户端资源、私有技能数据库和本地开发历史。成品中的界面作为程序资源内嵌分发。

本仓库可以编译核心静态库并运行测试，**无法据此构建完整桌面软件**。需要使用软件时请下载 Release。

## 编译核心库

依赖：Windows x64、CMake 3.20+、支持 C++20 的编译器、zlib、nlohmann/json。无需 UI、WebView2 SDK、SkillImgDb.dll、FFmpeg、客户端或私有工程。

验证环境为 LLVM MinGW 20260616 UCRT、zlib 1.2.11、nlohmann/json 3.12.0。将 LLVM MinGW 的 `bin` 加入 PATH，准备 zlib 头文件和静态库，以及包含 `nlohmann/json.hpp` 的目录，然后运行：

```powershell
cmake -S . -B build -G "MinGW Makefiles" `
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ `
  -DZLIB_INCLUDE_DIR=C:/deps/zlib -DZLIB_LIBRARY=C:/deps/zlib/libz.a `
  -DPRISM_JSON_INCLUDE=C:/deps/json/include
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

依赖官方来源：[LLVM MinGW](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260616)、[zlib](https://github.com/madler/zlib/tree/v1.2.11)、[nlohmann/json](https://github.com/nlohmann/json/releases/tag/v3.12.0)。源码库不附带工具链或预编译依赖。

产物为 `prism_core`、`dnflib` 静态库与两个测试程序。从 `native/prism_core.hpp` 的 `ColorSettings`、`recolor`、`bakeImg`、`DyeSnapshot`、`writePatch` 开始阅读；预设存储位于 `native/preset_store.hpp`。

核心头文件中保留了桌面集成所用的 `SkillDatabase` 类型声明，但本仓库不提供其私有数据库实现。REP 目录扫描可使用接收外部名称映射路径的 `Catalog` 构造函数；无参构造函数依赖成品内嵌的技能名称资源。

## 许可与来源

项目尚未指定通用开源许可证。核心源码公开不表示私有界面或成品的实现一并开放授权。第三方代码保留各自原始许可；参见 `THIRD_PARTY_NOTICES.txt` 和相应源文件中的声明。客户端及游戏资源的权利属于相应权利人。

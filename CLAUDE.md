# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

MusicPlayer2 —— 使用 C++/MFC 编写的 Windows 本地音乐播放器。原生桌面程序，不使用任何包管理器，所有第三方库均以源码/静态库形式内置在仓库中。

注意目录嵌套：仓库根目录存放 `MusicPlayer2.sln`，程序源码在下一级的 `MusicPlayer2/` 目录中。

## 编译

需要 Visual Studio 2022（`v143` 工具集）并安装 **C++ MFC/ATL** 组件 —— 所需组件的准确 ID 见 `.github/workflows/ci.yaml`。项目使用 `stdcpplatest`、Unicode 字符集、动态链接 MFC，通过 `stdafx.h` 使用预编译头。

```pwsh
# 在仓库根目录执行（msbuild 会自动找到 .sln）
msbuild -t:Build '-p:Configuration=Release;platform=x64' -m:4
msbuild -t:Build '-p:Configuration=Release;platform=x86' -m:4   # vcxproj 内部名为 'Win32'
```

输出目录在仓库根目录下：`x64/Release/`、`x64/Debug/`（x64）和 `Release/`、`Debug/`（x86）。

编译出的 exe 需要把 `MusicPlayer2/language/` 复制到同级目录，否则界面上所有字符串都会显示为 `error_str`。CI 在编译后有专门的复制步骤。

生成前事件会执行 `print_compile_time.bat`，把时间戳和 git HEAD 写入 `compile_time.txt`，资源编译器再从 `$(IntDir)` 读取它。没有 git 环境时该步骤会安全跳过。

解决方案中还包含 `scintilla/win32/SciLexer.vcxproj` —— 歌词编辑器使用的编辑控件。

## 测试

**没有可用的自动化测试。** `UnitTest/` 是一个 MSTest（`CppUnitTest`）工程，只含一条无意义的断言，而且**没有被 `MusicPlayer2.sln` 引用**，既不参与编译也不会运行。不要把它当作质量门禁；如果要在其中补充测试，必须先把该工程加入解决方案。

验证方式是手动的：编译、运行、实际操作功能。程序自带调试入口 —— `skins/00_uiTest.xml` 配合 `UIDialog/UITestDialog` 用于测试界面元素，`TestDlg`/`CTest` 用于临时验证。

CI 仅在推送 `v*` 标签或手动触发时运行，且只编译 Release 版本。

## 架构

### 全局状态集中在 `theApp`

`CMusicPlayerApp`（`MusicPlayer2.cpp` 中的全局对象 `theApp`）是整个程序的枢纽。它持有所有设置结构体（`m_app_setting_data`、`m_play_setting_data`、`m_media_lib_setting_data` 等，全部定义在 `CommonData.h`）、所有解析好的路径、字符串表、图标/菜单管理器、字体和图片资源集。代码通过 `theApp.` 访问配置和共享资源，而不是层层传参。

### 播放器模型与可替换的播放内核

`CPlayer`（`Player.h/.cpp`）是播放模型：以 `vector<SongInfo>` 保存播放列表，以及当前序号、播放状态、频谱数据、均衡器/混响、专辑封面、歌词。它持有一个 `IPlayerCore*`。

`IPlayerCore`（`IPlayerCore.h`）是音频后端接口 —— 打开/播放/定位、音频信息与标签探测、FFT 频谱数据、格式转换编码。共三个实现，可在设置中运行时切换：`CBassCore`（BASS，默认）、`CFfmpegCore`、`CMciCore`。`MusicPlayer2/Plugins/*.dll` 中的 BASS 插件通过继承 `DllLib` 的轻量封装类（`BASSMidiLibrary`、`BassFxLibrary`、`BASSEncodeLibrary` 等）懒加载。**新增一个播放内核只需实现 `IPlayerCore`，不涉及其他改动。**

播放列表的加载在工作线程中进行（`CPlayer::IniPlaylistThreadFunc`），进度和完成状态通过 `Player.h` 顶部定义的 `WM_PLAYLIST_INI_*` / `WM_MUSIC_STREAM_OPENED` 等自定义消息回传给主窗口。

`CMusicPlayerDlg`（`MusicPlayerDlg.cpp`，约 300KB，仓库中最大的文件）是主窗口和命令分发中心。较重的命令逻辑拆分到了 `MusicPlayerCmdHelper` 和 `UIWindowCmdHelper`。

### 两套并存的 UI 体系

**1. XML 自定义界面（主界面）。** `CPlayerUIBase` 是绘制基类，`CUserUi` 从 XML 构建元素树。界面元素是 `UIElement/` 下的各个类，而 `UIElement/ElementFactory.cpp` 是把 XML 标签名映射到类的**唯一入口** —— 新增元素 = 在 `UIElement/` 加一个类 + 在工厂里加一个 `else if`（并建议同步更新 `skins/skin.xsd`）。界面文件在启动时从 `theApp.m_local_dir + "skins\\*.xml"` 扫描加载，另有两个内置界面以资源 `IDR_UI1`/`IDR_UI2` 形式嵌入。可复用的组合元素在 `UIElement/CombinedElement`，面板在 `UIPanel/` 下由 `PanelManager` 管理。

**2. 传统 MFC 对话框。** 所有对话框都继承 `CBaseDialog`，由它统一处理窗口尺寸记忆、最小尺寸、布局重排和图标。两个关键重写：

- `GetDialogName()` —— 保存窗口尺寸时使用的配置键名。
- `InitializeControls()` —— 在此处从字符串表设置**全部**控件文本，然后调用 `RepositionTextBasedControls()`，使控件宽度在翻译后仍然合适。若设置了最小尺寸则返回 `true`。

`AiSettingDlg.cpp` 是这套写法较新且简洁的参考样例。

### 多语言是强制要求

任何用户可见的字符串都不能硬编码。`StrTable`（`theApp.m_str_table`）加载 `MusicPlayer2/language/*.ini`：

- `LoadText(L"TXT_…")` / `LoadMenuText(menu, key)` —— 普通文本
- `LoadTextFormat(L"MSG_…", { 参数, … })` —— 含 `<%1%>` 占位符的文本

ini 的节为 `[text]`、`[scintlla]`，以及每个菜单一个 `[menu.<菜单名>]`。**四个语言文件必须同步修改：`English.ini`、`Simplified_Chinese.ini`、`Traditional_Chinese.ini`、`Russian.ini`。** 缺失的键会返回 `StrTable::error_str`，并在程序退出时写入日志（`GetUnKnownKey()`）。

### 菜单在代码中构建，不在 .rc 中

`MenuMgr`（`MenuMgr.cpp`）定义了所有菜单：`MenuType` 枚举列出每个菜单，`MenuMgr::CreateMenu` 通过 `menu.AppendItem(EX_ID(ID_X), IconMgr::IconType::IT_Y)` 逐项填充。菜单文本来自 ini 的 `[menu.*]` 节，图标来自 `IconMgr`（负责句柄缓存与懒加载）。

新增一个菜单命令需要改动：`resource.h` 中的 ID、`MusicPlayer2.rc` 中的命令项、`MenuMgr::CreateMenu` 中的 `AppendItem` 调用、四个语言 ini 中的文本，以及所属窗口消息映射中的处理函数。

### 媒体库与数据持久化

- `CSongDataManager` —— 单例，以 `unordered_map<SongKey, SongInfo>` 保存所有已知曲目，通过 MFC `CArchive` 序列化到 `theApp.m_song_data_path`。
- `CRecentList` —— 统一管理最近播放的文件夹/播放列表/媒体库项目，统一建模为 `ListItem`。
- 设置通过 `CIniHelper` 保存到 `theApp.m_config_path` 的 ini 文件；界面状态保存到 `m_ui_data_path`。
- `CTagLibHelper` 封装内置的 taglib 用于标签读写。`AudioTagOld.cpp` 是旧实现，仍保留用于 taglib 无法处理的格式。

### 网络请求

所有 HTTP 请求统一走 `CInternetCommon::SendHttpRequest` / `HttpGet` / `HttpPost`（基于 WinInet，输入输出均为宽字符串）—— 不要引入第二套 HTTP 实现。歌词和封面下载源（`NeteaseLyricDownload`、`QQMusicLyricDownload`）位于 `CLyricDownloadCommon` 之后；这些第三方 API 随时可能变动，这也是更新日志中反复出现下载失败修正的原因。

### 内置第三方库

全部内置在仓库中，无需额外拉取：`bass.h` + `bass.lib`/`bass_x64.lib`（BASS 音频库，运行时 DLL 在 `Plugins/` 和 `Encoder/`）、`taglib/` + `tag.lib`/`tag_x64.lib`、`tinyxml2/`、`nlohmann/`（JSON）、`scintilla/`、`md5.cpp`。链接项只有 `bass*.lib` 和 `Powrprof.lib`。

## 代码约定

- **注释和文档注释使用中文**，请与所修改文件保持一致。
- 4 空格缩进，成员变量 `m_` 前缀，类名使用 `CFoo` 风格，成员使用类内初始化（`int m_x{ 0 };`）。
- 每个 `.cpp` 的第一个 include 都是 `stdafx.h`。
- `Define.h` 已统一引入 STL 并 `using std::wstring/vector/map/...`，因此代码中直接使用不带命名空间的 `wstring`、`vector`、`map`。所有文本均使用 `wstring`（纯 Unicode 编译）。
- `resource.h` 和 `MusicPlayer2.rc` 由 VS 资源编辑器维护，`.rc` 文件约 270KB，手工修改时务必只改局部并保持 ID 块的顺序。
- 更新日志在 `Documents/update_log.md` 和 `update_log_en-us.md`（需同步），发布信息在 `version.info`。

## 正在进行中的改动（尚未提交）

工作区中有一个「AI 自动整理歌曲」功能，处于未跟踪/已修改状态：

- `AiClient.{h,cpp}` —— 支持三种接口格式的 LLM 客户端（OpenAI 兼容、Google Gemini、Anthropic），配置保存在 ini 的 `ai_organize` 节，JSON 使用 `nlohmann`，HTTP 使用 `CInternetCommon`。
- `AiSettingDlg`（服务商/模型/密钥配置、连接测试、拉取模型列表）和 `AiSongOrganizeDlg`（基于 `AiSongInfo` 结果的批量标签整理）。
- 接入点：`MenuMgr::CreateMenu` 中的 `ID_TOOL_AI_ORGANIZE`、`MusicPlayerDlg` 中的处理函数、`theApp` 上的 `m_ai_organize_dialog_exit` 标志、`MusicPlayer2.rc` 中的对话框模板，以及四个语言 ini 中的 `TXT_AI_*`/`MSG_AI_*` 键。

该功能遵循上述对话框与多语言约定，继续扩展时请保持一致。

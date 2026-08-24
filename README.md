# PAL4 Inject Track

`inject/` 是原始 `launch.exe` 的 x86 注入与替换框架。

## 目标
- 保持 `rebuild/` 专注 IDA-backed truth recovery。
- 新增一条独立的运行时 seam，用 DLL 注入逐步把原始 EXE 中的函数替换到可控的 C++ 模块里。
- v1 优先拿到菜单/UI 自动化能力，首个真正被 DLL 取代的函数是：
  - `ProcessUIEvent @ 0x411900`

## 目录
- `include/pal4inject`
  - 公共类型、协议、地址表、Hook inventory、输入逻辑
- `src/common`
  - launcher helper、协议编解码、纯逻辑工具
- `src/runtime`
  - DLL bootstrap、hook、IPC、输入替换
- `tests`
  - 单元测试与可选的原始 EXE 集成测试
- `docs`
  - 架构、Hook inventory、launcher 说明与第三方声明文档

## 构建
必须使用 Win32/x86 生成器。

```powershell
git submodule update --init --recursive
cmake -S I:\PAL4\projects\pal4_inject -B I:\PAL4\projects\pal4_inject\build -A Win32 -DPAL4_INJECT_ENABLE_DEV_PUBLISH=OFF
cmake --build I:\PAL4\projects\pal4_inject\build --config Debug
```

开发构建默认不会覆盖游戏目录。只有需要将构建产物自动复制到
`I:\Games\original` 时，才显式传入 `-DPAL4_INJECT_ENABLE_DEV_PUBLISH=ON`。

## 启动
- 兼容旧方式：
  - `PAL4_inject.exe --game-root <包含 launch.exe 的目录>`
- 新增直接指定目标 EXE：
  - `PAL4_inject.exe --exe <完整 exe 路径>`
- 新增脚本模式切换：
  - `--script-mode cs`
  - `--script-mode csb`
  - 不传参数直接双击 `PAL4_inject.exe` 时，会弹出中文 GUI 选择 `CS` 或 `CSB`；首次默认 `CSB`，后续恢复上次启动时的选择
- 发布启动入口：
  - 发布使用时，把 `dist` 目录里的文件复制到 PAL4 游戏安装目录
  - `PAL4_inject.exe` 放在游戏目录根部，和 `PAL4.exe` 同级
  - `PAL4_inject.exe` 是 GUI 程序，双击启动时不会弹出 CMD 黑窗口
  - 注入相关文件放在游戏目录下的 `pal4_inject` 子目录，便于后续覆盖更新
  - 注入配置、runtime log、crash report / dump 等运行产物也统一放在 `pal4_inject` 子目录
  - 双击 `PAL4_inject.exe` 后由 GUI 选择 `CS` 或 `CSB`
  - GUI 会读取并保存游戏目录下的 `config.cfg`，可设置分辨率、普通窗口/无边框窗口/独占全屏、宽屏和垂直同步
  - 设置导航按 `游戏 / 视频 / 音频 / 控制 / 增强 / 高级` 分类；显示相关选项统一放在“视频”
  - “音频”页可在 `0%–300%` 范围独立设置 `giTalk` 对白配音音量；游戏中按 `[` / `]` 可降低 / 提高 5%，屏幕顶部会短暂显示矢量喇叭、音波档位和百分比数字；只调整 `PALSOUND` 配音对象，不改变 BGM 或普通音效
  - “控制”页支持 Xbox 360 / XInput 手柄按钮映射；现代控制模式使用左摇杆连续方向与推动幅度切换走/跑，右摇杆调整视角，并可实验性保持自由镜头模式
  - 自动检测全部显示器；无边框窗口可选择并记住所用显示器，D3D9 保持窗口呈现并覆盖其完整区域
  - 分辨率列表分为“常用分辨率”和“所选显示器支持”
  - GUI 打开时会自动检查一次更新，也提供“检查更新”按钮；会优先读取 Gitee 最新 Release，并以 GitHub 作为兜底；有新版时可打开下载页面
  - GUI 提供“反馈 Bug”页：检测最新崩溃文本，允许预览并选择附带脱敏后的崩溃报告或运行日志末尾；只有用户明确勾选授权后才会把诊断内容带到 Gitee 新建 Issue 页面
  - 第一版不会读取或上传 minidump，也不会在后台自动提交；完整正文会同时复制到剪贴板，最终由用户在 Gitee 页面检查并提交
  - GUI 右上角显示当前版本和作者信息，点击 `B站 @北风7P` 可打开作者主页
  - 当前内置版本为 `v0.2.0`，发布 Release 时建议使用同名 tag；构建号只用于定位具体构建时间

示例：

```powershell
I:\PAL4\projects\pal4_inject\build\Debug\PAL4_inject.exe `
  --exe I:\Games\original\PAL4.exe `
  --script-mode cs `
  --dll I:\PAL4\projects\pal4_inject\build\Debug\runtime.dll
```

脚本模式切换当前通过启动器在进程恢复前写入 `launch.exe` 的
`g_IsCSBMode @ VA 0x8C27FC`：

- `cs` -> 写 `0`
- `csb` -> 写 `1`

启动器还会把请求的脚本模式通过继承环境变量传给子进程。
注入后的 bootstrap 会再次读取实际值，必要时补写，并把
`requested_script_mode / script_mode / script_mode_flag`
导出到 `read_ui_state` 快照里。

## 松散文件优先补丁

把新增或修改的关卡资源按完整游戏内路径放到：

```text
<游戏目录>\gamepatch\gamedata\...
```

例如 `gamedata\PALWorld\Q99\Q99\A.dff` 对应：

```text
<游戏目录>\gamepatch\gamedata\PALWorld\Q99\Q99\A.dff
```

CS 文本脚本使用同一规则，例如 `gamedata\editData\script\M10.cs` 放到：

```text
<游戏目录>\gamepatch\gamedata\editData\script\M10.cs
```

CPK 资源不存在补丁或加载失败时仍可回退原 CPK。CS 模式不同：发行版不携带 `editData` 文本源码，启用松散文件补丁后只从 `gamepatch` 读取；缺少脚本或读取失败会直接报失败，不会尝试游戏目录原路径。“增强”页使用一个统一开关，所有覆盖、缺失、关闭绕过和 CPK 回退都会写入 `gamepatch\loose_file_load.log`（JSON Lines）。完整规则见 [docs/loose_file_overlay.md](docs/loose_file_overlay.md)。

## 产物
- `runtime.dll`
- `cli.exe`
- `PAL4_inject.exe`
  - 启动器 EXE 内嵌 `assets/icons/xianjian_syringe_energy.ico` 作为程序图标
- `pal4_inject_tests.exe`

## 发布脚本
一键构建 Release、运行测试、刷新 `dist`、生成 zip，并创建或更新 GitHub / Gitee Release：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\release.ps1
```

脚本默认从 `CMakeLists.txt` 读取版本号，例如 `0.2.0` 会生成 tag/release 版本 `v0.2.0`，产物为 `PAL4_inject_v0.2.0_win32.zip`。如只想本地打包、不发布 GitHub Release：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\release.ps1 -SkipGitHubRelease -SkipGiteeRelease
```

自动化验证时使用 `--background`。运行时会拦截 PAL4 主窗口的激活请求，使窗口保持在
当前工作窗口后方且不抢占焦点；配合 `cli click --direct-seam` 可完整驱动 UI。
`--minimized` 作为兼容别名保留，因为原版真正最小化时会触发 RenderWare 初始化或主循环异常。

如需显式附带发布说明，可传入：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\release.ps1 -ReleaseNotesPath .\docs\release_notes_v0.2.0.md
```

如果 `dist\PAL4.exe` 已存在，脚本会在刷新 `dist` 时保留它，并把它一同打入发布 zip；不要把 `PAL4.exe` 提交进 git。

发布前脚本会要求没有未提交的源码改动；如本地不存在同名 tag，会自动在当前提交创建 tag，并推送当前分支和 tag。Gitee 发布需要设置 `GITEE_TOKEN` 或 `GITEE_ACCESS_TOKEN`，也可以传 `-GiteeAccessToken <token>`；如只发布 GitHub，可加 `-SkipGiteeRelease`。

## 当前范围
- launcher 采用 suspended 启动 + `LoadLibraryW` 远程线程注入。
- runtime DLL 通过 named event + named pipe 暴露 ready 信号、agent/control CLI 和测试控制面。
- ImGui launcher 在启动游戏前统一管理分辨率、脚本模式、MSAA 与各项 Hook 配置。
- runtime DLL 不再创建游戏内 Inject 面板，也不再安装面板专用的窗口焦点 Hook。
- 注入配置保存在 `pal4_inject\inject_settings.ini`；旧的 `inject_panel_settings.ini` 仅作兼容读取。
- Hook 框架内置 x86 inline detour，不依赖第三方 Hook 库。
- `cli.exe` 会复用同一条 named pipe，提供：
  - `snapshot / click / click-path / fill / type / press / hold`
  - `state / paliv / event-log`
  - `wait-path / wait-text / wait-event / wait-state / wait-paliv / wait-hook`
  - `click-path` 每次操作前重抓 snapshot，可用完整路径或唯一的 `/` 分段后缀；后缀有歧义时拒绝点击。
  - `paliv` 与 `wait-paliv` 只读取游戏主线程 input hook 已发布的状态，不在 IPC worker 线程直接调用 PAL4 accessor，避免启动早期 UI 尚未创建时的跨线程崩溃。
  - `mem-query / mem-read / mem-read-scalar / mem-write-bytes / mem-write-scalar`
- bootstrap 早期安装 crash capture：
  - `AddVectoredExceptionHandler`
  - `SetUnhandledExceptionFilter`
  - 崩溃时把文本报告和 minidump 写到游戏目录下的 `pal4_inject`
- bootstrap 期间会自动应用一个最小相机补丁：
  - 把主游戏流程里的竖直 pitch 相对窗口从 `±20` 放宽到 `±89`
  - 把竖直鼠标缩放对齐到横向鼠标缩放
  - 通过 `Camera_UpdateMatrix @ 0x5EA190` 统一把最终 second angle 夹回安全区 `[0,89] U [271,360)`，避免越过 `90` 度后的视角翻面
- v1 默认安装这些 Hook：
  - `PackageResourceManager_OpenFile` loose-file overlay seam
  - `cs_TextScriptInterpreter_Initialize` CS text-script loose-file seam（跟随同一个 UI 开关）
  - `ProcessUIEvent`
  - `HandleUIMessageAndProcess`
  - `SimulateKeyPressAndRelease`
  - `ProcessInputs`
  - `UpdateInputDeviceState`
  - `InitializeDirectInput`
  - `giTalk` 脚本执行入口
  - `giPlayMovie` 资源请求观测
  - `AudioSystem_PlayMusic`（兼容保留的旧 Hook ID，实际为 `AudioSystem_PlaySample`）对白 MP3 打开观测与对象级音量倍率
  - `BinkOpen` 包装层打开结果观测
  - `CEGUI_Renderer_Constructor_2` widescreen pillarbox patch
  - `SetupMinimapTexture` widescreen layout patch
  - `Camera_UpdateMatrix` second-angle guard
  - `D3D9SetPresentParameters` multisample override seam
- `HandlePlayerInputEvents` 目前只保留枚举和兼容解析，不进入 launcher 功能清单。

媒体观测使用不可关闭的低频关键事件，供后台原版验收读取：`media=voice event=open ... success=1`、`media=bink event=request resource=...`、`media=bink event=open success=1`、`media=bink event=start ...`。普通 hook 调试日志仍由各行 `log_enabled` 控制，避免逐帧日志淹没事件尾。

## Agent / Debug CLI
- 连接方式：
  - `cli.exe --pipe <named-pipe> <command>`
  - `cli.exe --pid <game-pid> <command>`
- 常用 UI 闭环：

```powershell
I:\PAL4\projects\pal4_inject\build\Debug\cli.exe --pid 1234 snapshot
I:\PAL4\projects\pal4_inject\build\Debug\cli.exe --pid 1234 click e7
I:\PAL4\projects\pal4_inject\build\Debug\cli.exe --pid 1234 press Escape
```

- 常用内存调试：

```powershell
I:\PAL4\projects\pal4_inject\build\Debug\cli.exe --pid 1234 mem-query --ida 0x8C27FC
I:\PAL4\projects\pal4_inject\build\Debug\cli.exe --pid 1234 mem-read --ida 0x8C27FC --size 4
I:\PAL4\projects\pal4_inject\build\Debug\cli.exe --pid 1234 mem-write-scalar --ida 0x8C27FC --type u32 1
```

- `snapshot` 会输出 CEGUI 当前窗口树，并给每个节点分配 `e1/e2/...` 引用。
- `click/fill` 只接受最近一次 `snapshot` 生成的 ref；重新 `snapshot` 后旧 ref 会失效。
- `mem-write-*` 默认允许数据页写入；若目标页可执行，必须显式加 `--unsafe-code-write`。
- `state` 除了原有 runtime 状态，还会导出 `main_module_base`，方便 `IDA EA -> runtime VA` 换算。

## Widescreen UI
- 对 `1280x800`，原 EXE 已有专门的宽屏 renderer 变体，保持原行为。
- 对共享 `CEGUI_Renderer_Constructor_2` 路径上的宽屏分辨率（例如 `1600x900`、`1680x1050`、`1920x1080`）：
  - 注入 runtime 会把 UI 改成按高度等比缩放
  - 计算左右 pillarbox 留白
  - 居中显示 4:3 逻辑 UI，而不是把 UI 横向拉伸铺满整个窗口
- 游戏内常驻 HUD 会额外补一层“贴边”修正，尽量接近原版 `1280x800` 的宽屏摆法：
  - `minimap.xml` 可见部件改为靠左下
  - `portrait.xml` 可见部件改为靠右上
- 小地图纹理区域也会同步按同一套宽屏 plan 重定位：
  - 不再继续按“居中 4:3 UI 框”的偏移去摆放小地图图片
  - 改成和左下角 HUD 框体对齐，避免图像仍停在屏幕中部

## Crash Capture
- runtime 常规日志：
  - `pal4_inject\\pal4_inject_runtime.log`
- 崩溃文本报告：
  - `pal4_inject\\pal4_inject_crash_pid*_tid*_code*_tick*.txt`
- 崩溃 minidump：
  - `pal4_inject\\pal4_inject_crash_pid*_tid*_code*_tick*.dmp`
- `read_ui_state` 快照会额外暴露：
  - `crash_handler_ready`
  - `last_crash_report_path`
  - `last_crash_dump_path`
  - `last_crash_summary`
  - `borderless_window_enabled / borderless_monitor / borderless_window_applied / borderless_window_summary`
  - `gi_talk_volume / gi_talk_volume_applied / gi_talk_volume_summary`
- launcher 的“反馈 Bug”页会扫描上述最新文本报告，并在本地完成路径和敏感字段脱敏：
  - 游戏安装目录替换为 `<游戏目录>`
  - Windows 用户目录替换为 `<用户目录>`
  - `access_token / authorization / password / api_key / client_secret` 等字段值隐藏
  - 崩溃文本和 runtime log 必须逐项勾选；存在诊断内容时还必须额外确认上传授权
  - `.dmp` 只提示存在，第一版不读取、不复制、不上传

## Launcher 配置

- 详细中文说明见 [docs/launcher_guide.md](I:/PAL4/projects/pal4_inject/docs/launcher_guide.md)。
- “视频”页统一管理显示器、窗口模式、分辨率、宽屏、Bink 显示方式与 MSAA；“音频”页提供独立对白语音音量；“增强”页保留相机和资源覆盖等功能。
- 宽屏 UI、字体、小地图、战斗界面和 Bink 修正由所选分辨率自动控制：宽于 `4:3` 时开启，`4:3` 或更窄时关闭。
- “高级调试”页仍可逐项设置 `HookMode` 和详细日志。
- MSAA、对白语音音量和 Hook 选项在游戏启动前写入配置，runtime 在安装 Hook 前读取。

## 测试
- `pal4_inject_tests.exe`
  - 默认跑纯单元测试
  - 若设置 `PAL4_INJECT_RUN_INTEGRATION=1` 且 `PAL4_GAME_ROOT` 指向真实游戏目录，则追加：
    - `snapshot_ui` 看见 `BtnNewGame / BtnExit`
    - `mem-read --ida` 与 `mem-read --va` 一致性
    - 安全数据页写入 + 恢复
    - 代码页无 `unsafe` 写入拒绝
  - 若额外设置 `PAL4_INJECT_RUN_FULL_SCENARIOS=1`，则继续跑 `snapshot -> click` 驱动的 `NewGame` / `Exit` 完整菜单场景

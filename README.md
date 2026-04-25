# PAL4 Inject Track

`inject/` 是原始 `launch.exe` 的 x86 注入与替换框架。

## 目标
- 保持 `rebuild/` 专注 IDA-backed truth recovery。
- 新增一条独立的运行时 seam，用 DLL 注入逐步把原始 EXE 中的函数替换到可控的 C++ 模块里。
- v1 优先拿到菜单/UI 自动化能力，首个真正被 DLL 取代的函数是：
  - `ProcessUIEvent @ 0x411900`

## 目录
- `include/pal4`
  - 随仓库内置的 PAL4 layout/evidence 头文件快照，保证子模块可单独编译
- `include/pal4inject`
  - 公共类型、协议、地址表、Hook inventory、输入逻辑
- `src/common`
  - launcher helper、协议编解码、纯逻辑工具
- `src/runtime`
  - DLL bootstrap、hook、IPC、输入替换
- `tests`
  - 单元测试与可选的原始 EXE 集成测试
- `docs`
  - 架构、Hook inventory、控制面板、手柄说明和调查文档

## 构建
必须使用 Win32/x86 生成器。

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Debug
```

当前仓库已内置 `include/pal4/runtime_layouts.h` 与 `include/pal4/evidence_status.h`
这两份 PAL4 layout/evidence 头文件快照，因此不再要求父目录存在
旧外部 include 目录才能完成编译。

默认情况下，`cmake --build` 完成后还会自动执行一层部署同步：
- 把 `PAL4_inject.exe`、`cli.exe`、`runtime.dll` 同步到 `dist`
- 再同步到本地测试目录 `I:\Games\original`

这个行为由 CMake 选项控制：

```powershell
cmake -S . -B build -A Win32 `
  -DPAL4_INJECT_SYNC_TEST_DEPLOY=ON `
  -DPAL4_INJECT_TEST_GAME_ROOT=I:\Games\original
```

如果只想构建、不自动复制：

```powershell
cmake -S . -B build -A Win32 -DPAL4_INJECT_SYNC_TEST_DEPLOY=OFF
```

## 启动
- 兼容旧方式：
  - `PAL4_inject.exe --game-root <包含 launch.exe 的目录>`
- 新增直接指定目标 EXE：
  - `PAL4_inject.exe --exe <完整 exe 路径>`
- 新增脚本模式切换：
  - `--script-mode cs`
  - `--script-mode csb`
  - 不传参数直接双击 `PAL4_inject.exe` 时，会弹出中文 GUI 选择 `CS` 或 `CSB`；首次默认 `CSB`，之后会记住上次选择
- 发布启动入口：
  - 发布使用时，把 `dist` 目录里的文件复制到 PAL4 游戏安装目录
  - `PAL4_inject.exe` 放在游戏目录根部，和 `PAL4.exe` 同级
  - `PAL4_inject.exe` 是 GUI 程序，双击启动时不会弹出 CMD 黑窗口
  - 注入相关文件放在游戏目录下的 `pal4_inject` 子目录，便于后续覆盖更新
  - 注入配置、runtime log、crash report / dump 等运行产物也统一放在 `pal4_inject` 子目录
  - 双击 `PAL4_inject.exe` 后会进入一个常驻中文启动器，不再是“一次性选完就退出”的对话框
  - 启动器按页签拆成“启动前准备 / 显示与分辨率 / 画面与文字 / 实验性功能 / 声明”五块，把需要在进游戏前决定的选项都集中到这里
  - 启动器会读取并保存游戏目录下的 `config.cfg`，可设置分辨率、全屏/窗口化、宽屏和垂直同步
- 启动器也会读取并保存 `pal4_inject\inject_panel_settings.ini` 里的启动前画质策略，可设置 `MSAA`、人物阴影分辨率、`UI` 像素采样、对白高清实验、系统字体高清实验，以及“坐姿 VR（实验性）”相机骨架开关
  - 分辨率列表仍保留“常用分辨率”和“主显示器支持”两个页签
  - 稳定画质项留在“画面与文字”，而 VR / 头姿态研究一类尚在验证中的能力统一放到“实验性功能”页，避免和日常配置混在一起
  - 启动器底部提供 `启动游戏 / 停止游戏 / 重启游戏 / 检查更新 / 最小化` 按钮；其中“最小化”用于收起窗口，右上角关闭按钮会直接退出启动器
  - GUI 打开时会自动检查一次更新，也提供“检查更新”按钮；会优先读取 Gitee 最新 Release，并以 GitHub 作为兜底；有新版时可打开下载页面
  - 作者、版本、更新入口、鸣谢和使用声明统一收拢到“声明”页，避免把这些说明散在主窗口顶栏
  - 当前内置版本为 `v0.1.4`，发布 Release 时建议使用同名 tag；构建号只用于定位具体构建时间

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

## 产物
- `runtime.dll`
- `cli.exe`
- `PAL4_inject.exe`
- `pal4_inject_tests.exe`

## 发布脚本
一键构建 Release、运行测试、刷新 `dist`、生成 zip，并创建或更新 GitHub / Gitee Release：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\release.ps1
```

脚本默认从 `CMakeLists.txt` 读取版本号，例如 `0.1.4` 会生成 tag/release 版本 `v0.1.4`，产物为 `PAL4_inject_v0.1.4_win32.zip`。如只想本地打包、不发布 GitHub Release：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\release.ps1 -SkipGitHubRelease -SkipGiteeRelease
```

如果 `dist\PAL4.exe` 已存在，脚本会在刷新 `dist` 时保留它，并把它一同打入发布 zip；不要把 `PAL4.exe` 提交进 git。

发布前脚本会要求没有未提交的源码改动；如本地不存在同名 tag，会自动在当前提交创建 tag，并推送当前分支和 tag。Gitee 发布需要设置 `GITEE_TOKEN` 或 `GITEE_ACCESS_TOKEN`，也可以传 `-GiteeAccessToken <token>`；如只发布 GitHub，可加 `-SkipGiteeRelease`。

## 当前范围
- launcher 采用 suspended 启动 + `LoadLibraryW` 远程线程注入。
- runtime DLL 通过 named event + named pipe 暴露 ready 信号、agent/control CLI 和测试控制面。
- runtime DLL 还会拉起一个原生 Win32 注入控制面板：
  - 默认隐藏，按 `Ctrl+J` 显示 / 隐藏
  - 默认会先跟随游戏窗口定位；用户手动拖动后就不再强制吸附
- 渲染与文字页顶部现在只显示“启动前画质设定摘要”，告诉你当前 `MSAA`、阴影、UI 采样、字体实验和坐姿 VR 实验链路的实际状态；这些设置统一改到启动器里配置
  - 每个 hook 一行，带快速开关、`HookMode` 下拉框和状态栏
  - 面板配置和启动器脚本模式会记忆到游戏目录下的 `pal4_inject\inject_panel_settings.ini`，下次启动自动恢复
  - `Ctrl+J` 显示 / 隐藏面板
  - 概览页会额外显示 `XInput` 手柄启用状态、连接状态和当前输入上下文
  - 作为游戏窗口的 owned popup 存在，避免点回游戏时像独立外部工具窗一样被压下去
- 当前内建一版不依赖 `Steam Input` 的 `XInput` 手柄支持：
  - 默认启用
  - 左摇杆映射基础场景移动
  - `A/B` 作为确认 / 取消
  - `X` 映射鼠标左键按住
  - `Y` 映射 `R`
  - `L3` 映射 `F`
  - `Back` 映射 `M`
  - `Start` 打开 / 关闭系统界面
  - `LB/RB` 切换系统主分页
  - `LT/RT` 切换纵向 `1/2/3` 子分页
  - 仅支持 `XInput` 设备，暂不支持右摇杆镜头、震动和自定义键位
- Hook 框架内置 x86 inline detour，不依赖第三方 Hook 库。
- `cli.exe` 会复用同一条 named pipe，提供：
  - `snapshot / click / fill / type / press`
  - `state / event-log / wait-path / wait-text`
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
  - `ProcessUIEvent`
  - `HandleUIMessageAndProcess`
  - `SimulateKeyPressAndRelease`
  - `ProcessInputs`
  - `UpdateInputDeviceState`
  - `InitializeDirectInput`
  - `giTalk` 脚本执行入口
  - `CEGUI_Renderer_Constructor_2` widescreen pillarbox patch
  - `SetupMinimapTexture` widescreen layout patch
  - `Camera_UpdateMatrix` second-angle guard
  - `D3D9SetPresentParameters` multisample override seam
- `PAL4_Main_WndProc` 和 `HandlePlayerInputEvents` 目前只保留 inventory，不默认安装。

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

## Inject Control Panel
- 详细中文说明见：
  - [docs/control_panel_guide.md](I:/PAL4/projects/pal4_inject/docs/control_panel_guide.md)
  - [docs/gamepad_guide.md](I:/PAL4/projects/pal4_inject/docs/gamepad_guide.md)
- 注入后会创建 `PAL4 Inject Control` 小面板，但默认隐藏；按 `Ctrl+J` 显示 / 隐藏。
- 面板的“渲染与画面”页现在先显示一块启动前设定摘要，用来确认本局实际生效的 `MSAA`、阴影、UI 采样和字体实验状态。
- 这些启动前设定已经迁移到启动器里统一管理，注入面板不再提供热切编辑。
- hook 列表区会显示：
  - 更短的可读名称
  - `On` 快速开关
  - 当前 mode 下拉框
  - installed / active / call count / error 状态
- 对已安装且允许切换的 hook，可以直接从面板开关或改 mode。
- 当前会立刻响应切换的重点功能包括：
  - `ProcessUIEvent`
  - `HandleUIMessageAndProcess`
  - `SimulateKeyPressAndRelease`
  - `giTalk`
  - `CEGUI_Renderer_Constructor_2` widescreen pillarbox
  - `Camera_UpdateMatrix` pitch guard
  - `D3D9SetPresentParameters` multisample override

## Investigation Notes
- 文本宽屏发糊调查：
  - [docs/text_rendering_investigation.md](I:/PAL4/projects/pal4_inject/docs/text_rendering_investigation.md)
- 人物影子渲染链调查：
  - [docs/shadow_rendering_investigation.md](I:/PAL4/projects/pal4_inject/docs/shadow_rendering_investigation.md)

## 测试
- `pal4_inject_tests.exe`
  - 默认跑纯单元测试
  - 若设置 `PAL4_INJECT_RUN_INTEGRATION=1` 且 `PAL4_GAME_ROOT` 指向真实游戏目录，则追加：
    - `snapshot_ui` 看见 `BtnNewGame / BtnExit`
    - `mem-read --ida` 与 `mem-read --va` 一致性
    - 安全数据页写入 + 恢复
    - 代码页无 `unsafe` 写入拒绝
  - 若额外设置 `PAL4_INJECT_RUN_FULL_SCENARIOS=1`，则继续跑 `snapshot -> click` 驱动的 `NewGame` / `Exit` 完整菜单场景

## Seated VR Skeleton

- 这一版先实现的是“坐姿 VR 相机骨架”，不是完整 OpenXR/HMD 输出。
- 启动器里的 `坐姿 VR（实验性）` 会打开一条新的相机/渲染帧 hook 链：
  - `Game_RenderFrame` 每帧采样当前活跃相机
  - `Camera_UpdateMatrix` 可叠加一份外部头姿态偏移
- 运行时可通过 CLI/IPC 注入头姿态，先验证“头部朝向/小幅位移 -> 相机矩阵”这条主链：

```powershell
I:\PAL4\projects\pal4_inject\build\Debug\cli.exe --pid 1234 vr-pose 3 -2 0 0.01 0 0
I:\PAL4\projects\pal4_inject\build\Debug\cli.exe --pid 1234 vr-reset
I:\PAL4\projects\pal4_inject\build\Debug\cli.exe --pid 1234 state
```

- `state` / `read_ui_state` 现在会额外导出：
  - `vr_mode`
  - `vr_pose_*`
  - `vr_camera_*`
- 当前阶段的定位是为后续 `OpenTrack / OpenXR` 接入准备相机主链，不包含双眼渲染、OpenXR swapchain 和 VR UI。

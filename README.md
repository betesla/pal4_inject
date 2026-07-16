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
  - 架构、Hook inventory 与 launcher 配置说明文档

## 构建
必须使用 Win32/x86 生成器。

首次拉取后先初始化固定版本的 Dear ImGui 子模块：

```powershell
git submodule update --init third_party/imgui
```

```powershell
cmake -S I:\PAL4\projects\pal4_re\inject -B I:\PAL4\projects\pal4_re\inject\build -A Win32
cmake --build I:\PAL4\projects\pal4_re\inject\build --config Debug
```

## 启动
- 兼容旧方式：
  - `PAL4_inject.exe --game-root <包含 launch.exe 的目录>`
- 新增直接指定目标 EXE：
  - `PAL4_inject.exe --exe <完整 exe 路径>`
- 新增脚本模式切换：
  - `--script-mode cs`
  - `--script-mode csb`
  - 不传参数直接双击 `PAL4_inject.exe` 时，会打开 ImGui 中文 launcher，默认使用 `CSB`
- 发布启动入口：
  - 发布使用时，把 `dist` 目录里的文件复制到 PAL4 游戏安装目录
  - `PAL4_inject.exe` 放在游戏目录根部，和 `PAL4.exe` 同级
  - `PAL4_inject.exe` 是 GUI 程序，双击启动时不会弹出 CMD 黑窗口
  - 注入相关文件放在游戏目录下的 `pal4_inject` 子目录，便于后续覆盖更新
  - 注入配置、runtime log、crash report / dump 等运行产物也统一放在 `pal4_inject` 子目录
  - launcher 使用 Dear ImGui Win32 + DirectX 9 后端，兼容 Windows 7 且不依赖额外 shader compiler DLL
  - “游戏设置”读写游戏目录下的 `config.cfg`，可设置脚本模式、分辨率、全屏、宽屏和垂直同步
  - “注入功能”读写 `pal4_inject\inject_settings.ini`，可设置 MSAA 与各项修复开关
  - “高级调试”提供 HookMode 和逐项日志开关；期望值在启动前保存，实际应用结果仍以 runtime 日志和 CLI 为准
  - 旧版 `inject_panel_settings.ini` 会被兼容读取，下次启动时迁移到新文件名
  - launcher 提供“检查更新”按钮；优先读取 Gitee 最新 Release，并以 GitHub 作为兜底
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
- runtime DLL 不再创建游戏内控制窗口；配置统一在 launcher 中完成，运行时诊断保留在日志、named pipe 和 CLI 中。
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
- `PAL4_Main_WndProc` 面板焦点保护 hook 已随游戏内面板移除；`HandlePlayerInputEvents` 仍只保留 inventory，不默认安装。

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

## Launcher 配置

- 详细中文说明见 [docs/launcher_guide.md](I:/PAL4/projects/pal4_inject/docs/launcher_guide.md)。
- 普通玩家使用“游戏设置”和“注入功能”即可。
- HookMode、逐项日志等开发选项集中在“高级调试”。
- 游戏运行期间如需观察 installed / call count / error 等实际状态，使用 `cli.exe state`、`cli.exe event-log` 或 runtime 日志。

## 测试
- `pal4_inject_tests.exe`
  - 默认跑纯单元测试
  - 若设置 `PAL4_INJECT_RUN_INTEGRATION=1` 且 `PAL4_GAME_ROOT` 指向真实游戏目录，则追加：
    - `snapshot_ui` 看见 `BtnNewGame / BtnExit`
    - `mem-read --ida` 与 `mem-read --va` 一致性
    - 安全数据页写入 + 恢复
    - 代码页无 `unsafe` 写入拒绝
  - 若额外设置 `PAL4_INJECT_RUN_FULL_SCENARIOS=1`，则继续跑 `snapshot -> click` 驱动的 `NewGame` / `Exit` 完整菜单场景

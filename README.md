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
  - 架构与 Hook inventory 文档

## 构建
必须使用 Win32/x86 生成器。

```powershell
cmake -S I:\PAL4\projects\pal4_re\inject -B I:\PAL4\projects\pal4_re\inject\build -A Win32
cmake --build I:\PAL4\projects\pal4_re\inject\build --config Debug
```

## 产物
- `pal4_runtime_x86.dll`
- `pal4_injector_launcher.exe`
- `pal4_inject_tests.exe`

## 当前范围
- launcher 采用 suspended 启动 + `LoadLibraryW` 远程线程注入。
- runtime DLL 通过 named event + named pipe 暴露 ready 信号和测试控制面。
- runtime DLL 还会拉起一个原生 Win32 注入控制面板：
  - 默认显示在游戏窗口右上角附近
  - 默认会先跟随游戏窗口定位；用户手动拖动后就不再强制吸附
  - 每个 hook 一行，可以直接改 `HookMode`
  - `Ctrl+F10` 隐藏 / 显示面板
  - 作为游戏窗口的 owned popup 存在，避免点回游戏时像独立外部工具窗一样被压下去
- Hook 框架内置 x86 inline detour，不依赖第三方 Hook 库。
- bootstrap 早期安装 crash capture：
  - `AddVectoredExceptionHandler`
  - `SetUnhandledExceptionFilter`
  - 崩溃时把文本报告和 minidump 写到 `%TEMP%`
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
- `PAL4_Main_WndProc` 和 `HandlePlayerInputEvents` 目前只保留 inventory，不默认安装。

## Widescreen UI
- 对 `1280x800`，原 EXE 已有专门的宽屏 renderer 变体，保持原行为。
- 对共享 `CEGUI_Renderer_Constructor_2` 路径上的宽屏分辨率（例如 `1600x900`、`1680x1050`、`1920x1080`）：
  - 注入 runtime 会把 UI 改成按高度等比缩放
  - 计算左右 pillarbox 留白
  - 居中显示 4:3 逻辑 UI，而不是把 UI 横向拉伸铺满整个窗口
- 小地图纹理区域也会同步按同一套宽屏 plan 重定位：
  - 不再继续按“整屏宽度缩放”去摆放小地图图片
  - 避免小地图内容超出已经居中后的 UI 边框

## Crash Capture
- runtime 常规日志：
  - `%TEMP%\\pal4_inject_runtime.log`
- 崩溃文本报告：
  - `%TEMP%\\pal4_inject_crash_pid*_tid*_code*_tick*.txt`
- 崩溃 minidump：
  - `%TEMP%\\pal4_inject_crash_pid*_tid*_code*_tick*.dmp`
- `read_ui_state` 快照会额外暴露：
  - `crash_handler_ready`
  - `last_crash_report_path`
  - `last_crash_dump_path`
  - `last_crash_summary`

## Inject Control Panel
- 注入后会出现 `PAL4 Inject Control` 小面板。
- 面板会列出当前 hook inventory，并显示：
  - hook 名称
  - 当前 mode 下拉框
  - installed / call count 状态
- 对已安装且允许切换的 hook，可以直接从面板改 mode。
- 当前会立刻响应切换的重点功能包括：
  - `ProcessUIEvent`
  - `HandleUIMessageAndProcess`
  - `SimulateKeyPressAndRelease`
  - `giTalk`
  - `CEGUI_Renderer_Constructor_2` widescreen pillarbox
  - `Camera_UpdateMatrix` pitch guard

## 测试
- `pal4_inject_tests.exe`
  - 默认跑纯单元测试
  - 若设置 `PAL4_INJECT_RUN_INTEGRATION=1` 且 `PAL4_GAME_ROOT` 指向真实游戏目录，则追加 logo/menu 注入 smoke
  - 若额外设置 `PAL4_INJECT_RUN_FULL_SCENARIOS=1`，则继续跑 `NewGame` / `Exit` 的完整菜单场景

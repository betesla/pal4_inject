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
- Hook 框架内置 x86 inline detour，不依赖第三方 Hook 库。
- v1 默认安装这些 Hook：
  - `ProcessUIEvent`
  - `HandleUIMessageAndProcess`
  - `SimulateKeyPressAndRelease`
  - `ProcessInputs`
  - `UpdateInputDeviceState`
  - `InitializeDirectInput`
- `PAL4_Main_WndProc` 和 `HandlePlayerInputEvents` 目前只保留 inventory，不默认安装。

## 测试
- `pal4_inject_tests.exe`
  - 默认跑纯单元测试
  - 若设置 `PAL4_INJECT_RUN_INTEGRATION=1` 且 `PAL4_GAME_ROOT` 指向真实游戏目录，则追加 logo/menu 注入 smoke
  - 若额外设置 `PAL4_INJECT_RUN_FULL_SCENARIOS=1`，则继续跑 `NewGame` / `Exit` 的完整菜单场景

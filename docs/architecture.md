# PAL4 Inject Architecture

## Goals
- 用独立的 `inject/` 轨承载原始 `launch.exe` 的 x86 运行时替换。
- 把“启动原 EXE、注入 DLL、安装 Hook、暴露测试控制面”从 `rebuild/` 和 `rewrite/` 中分离出来。
- 先从最小 UI seam 入手，再逐层把输入与更高层逻辑掏到 DLL / 新模块里。

## Runtime Shape
- `pal4_injector_launcher.exe`
  - `CreateProcessA(..., CREATE_SUSPENDED)`
  - 远程写入 `pal4_runtime_x86.dll` 路径
  - `CreateRemoteThread(LoadLibraryW)`
  - 等待 named event
  - 通过 named pipe 读取 `read_ui_state`
  - 确认 `bootstrap_ready=1` 后恢复主线程
- `pal4_runtime_x86.dll`
  - `DllMain` 只拉起 bootstrap thread
  - bootstrap thread 负责：
    - 记录主模块基址
    - 初始化 Hook inventory / runtime state
    - 启动 named pipe server
    - 安装 v1 Hook
    - 置位 ready event

## Module Split
- `src/common`
  - `types.cpp`
    - 公共 enum / struct string conversion
  - `hook_inventory.cpp`
    - IDA-backed hook metadata
  - `input_logic.cpp`
    - 可测试的 `ProcessUIEvent` 纯消息翻译逻辑
  - `protocol.cpp`
    - named pipe 文本协议编解码
  - `launcher.cpp`
    - 进程创建、远程注入、ready 检查、pipe 请求
- `src/runtime`
  - `runtime_state.cpp`
    - bootstrap / pipe / hook call count / last UI event / last error
  - `hook_manager.cpp`
    - x86 inline detour、trampoline、prologue 校验、卸载
  - `cegui_bindings.cpp`
    - CEGUIBase 导出解析
  - `input_hooks.cpp`
    - `ProcessUIEvent` 替换与 observe-only wrapper
  - `ipc_server.cpp`
    - `ping / hook_status / enqueue_ui_message / simulate_key / read_ui_state / read_paliv_state / set_hook_mode / shutdown`
  - `bootstrap.cpp`
    - bootstrap 主流程

## Current Behavior Boundary
- 已默认安装的 Hook：
  - `ProcessUIEvent`
  - `HandleUIMessageAndProcess`
  - `SimulateKeyPressAndRelease`
  - `ProcessInputs`
  - `UpdateInputDeviceState`
  - `InitializeDirectInput`
- 只做 inventory、不默认安装：
  - `PAL4_Main_WndProc`
  - `HandlePlayerInputEvents`
- `ProcessUIEvent` 当前是 `replace_with_fallback`
  - 已直接接管：
    - 键盘按下/抬起
    - 字符输入
    - 鼠标移动
    - 左/右/中键按下/抬起
    - 滚轮
    - `WM_MOUSELEAVE`
  - 其余消息仍回落到原始 trampoline

## Testing Strategy
- 单元测试默认只跑纯逻辑与协议测试。
- 真正的原始 EXE 注入 smoke 由环境变量显式开启：
  - `PAL4_INJECT_RUN_INTEGRATION=1`
  - `PAL4_GAME_ROOT=<真实游戏目录>`
- 完整菜单场景默认不在每次集成 smoke 中启用：
  - `PAL4_INJECT_RUN_FULL_SCENARIOS=1`
- 这样本地默认开发循环不依赖真实游戏窗口，也避免无意中启动长时间挂着的进程。

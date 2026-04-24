# PAL4 Inject Hook Inventory

## Scope
这份清单只记录 `inject/` 当前消费的 IDA-backed 地址与 patch metadata。

## Active v1 Hooks
- `ProcessUIEvent @ 0x411900`
  - mode: `replace_with_fallback`
  - patch span: `8`
  - reason: 最小可替换 UI seam，最适合先打通菜单自动化
- `HandleUIMessageAndProcess @ 0x4BF0E0`
  - mode: `replace_with_fallback`
  - patch span: `8`
- `SimulateKeyPressAndRelease @ 0x4BFD70`
  - mode: `observe_only`
  - patch span: `6`
- `ProcessInputs @ 0x4080B0`
  - mode: `observe_only`
  - patch span: `7`
- `UpdateInputDeviceState @ 0x407910`
  - mode: `observe_only`
  - patch span: `5`
- `InitializeDirectInput @ 0x407640`
  - mode: `observe_only`
  - patch span: `5`
- `giTalk script callback @ 0x5DFB10`
  - mode: `replace_with_fallback`
  - patch span: `8`
  - reason: `0x5D8DA0 giTalk` 是注册壳；真正吃对白文本参数的是已注册回调 `ProxyClass_Vtable12 @ 0x5DFB10`
- `CEGUI_Renderer_Constructor_2 @ 0x413580`
  - mode: `replace_with_fallback`
  - patch span: `8`
  - reason: 对共享 renderer ctor 路径上的宽屏分辨率补上“按高度等比缩放 + 左右 pillarbox + 居中”的 UI 渲染语义，避免 16:9 直接横向拉伸 UI
- `LoadFontFile @ 0x4BD3B0`
  - mode: `replace_with_fallback`
  - patch span: `7`
  - reason: 只在已知 dynamic UI 字体 `system / systemBold / dialog_simsun` 创建成功后，按中央有效 UI 区域尺寸重发 `setAutoScalingEnabled(true) / setNativeResolution(800,600) / notifyScreenResolution(...)`
  - note:
    - 当前把它当作“实验性、可选 bootstrap hook”放在稳定链之后安装
    - 原因是这条 seam 的 prologue 更容易受编译器生成的 SEH 立即数字节影响
    - 已在当前 PAL4 二进制中确认起始字节：
      - `6A FF 68 99 37 82 00 64 A1 00 00 00 00 50 64 89`
- `SetupMinimapTexture @ 0x40DE10`
  - mode: `observe_only`
  - patch span: `8`
  - reason: 小地图纹理区域原本仍按整屏宽度缩放与定位；宽屏下需要和左下角 HUD 锚点保持一致，避免内容仍停在居中的 UI 框附近
- `SetProperties_4C2550 @ 0x4C2550`
  - mode: `replace_with_fallback`
  - patch span: `8`
  - reason: 只在战斗相关调用点上改写 `x` 写入值，覆盖伤害数字、状态图标和 `zhangdoushengli` 胜利图，不去碰其它共享调用方
- `ui_showCombatHint @ 0x54A1F0`
  - mode: `replace_with_fallback`
  - patch span: `7`
  - reason: 浮动战斗提示窗在原始 `setPosition + 居中` 后仍停留在旧 4:3 逻辑坐标；宽屏下需要整体右移到中央 UI 框
- `ui_showCombatHint2 @ 0x54A960`
  - mode: `replace_with_fallback`
  - patch span: `7`
  - reason: 第二套战斗提示窗与上一条同类，也需要在宽屏时补中央 UI 偏移
- `Camera_UpdateMatrix @ 0x5EA190`
  - mode: `replace_with_fallback`
  - patch span: `7`
  - reason: 在矩阵重建前统一夹紧最终 second angle 到安全区 `[0,89] U [271,360)`，避免 second angle 穿过 `90` 度后的视角翻面
- `D3D9SetPresentParameters @ 0x75F710`
  - mode: `replace_with_fallback`
  - patch span: `10`
  - reason: 复用原始 D3D9 多重采样探测逻辑，在设备创建 / reset 前写入记忆下来的 `MSAA` 请求值
- `PAL4_Main_WndProc @ 0x40A170`
  - mode: `replace_with_fallback`
  - patch span: `8`
  - reason: panel 焦点切换后拦截最小化链，避免点击 inject panel 再切回游戏时窗口被最小化

## Reserved Hooks
- `HandlePlayerInputEvents @ 0x4283B0`
  - mode: reserved
  - patch span: `9`

## Helper Calls Used By `ProcessUIEvent`
- `MapVirtualKeyToUIKey @ 0x412130`
- `EnableMouseCapture @ 0x4120D0`
- `DisableMouseCapture @ 0x4120E0`
- `TransformMouseCoordinates @ 0x412320`
- `PALGameIV_GetInstance @ 0x5B5AF0`
- `UIFrameManager_GetInstance @ 0x4BB650`

## Evidence Notes
- 所有地址均已在 IDA 中重新确认。
- 这里的 prologue 指纹用于 runtime 安装前校验；若校验失败，Hook 必须拒绝安装。
- `inject/` 只消费这些常量，不把 `rebuild/src` 当实现依赖；当前仓库已内置最小必需的 PAL4 头文件快照。

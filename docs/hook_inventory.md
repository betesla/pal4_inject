# PAL4 Inject Hook Inventory

## Scope
这份清单只记录 `inject/` 当前消费的 IDA-backed 地址与 patch metadata。

## Active v1 Hooks
- `ProcessUIEvent @ 0x411900`
  - mode: `replace_with_fallback`
  - patch span: `8`
  - reason: 最小可替换 UI seam，最适合先打通菜单自动化
- `HandleUIMessageAndProcess @ 0x4BF0E0`
  - mode: `observe_only`
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
- `SetupMinimapTexture @ 0x40DE10`
  - mode: `replace_with_fallback`
  - patch span: `8`
  - reason: 小地图纹理区域原本仍按整屏宽度缩放与定位；宽屏下需要按同一套 pillarbox plan 重算位置和尺寸，避免内容溢出居中后的 UI 边框
- `Camera_UpdateMatrix @ 0x5EA190`
  - mode: `replace_with_fallback`
  - patch span: `7`
  - reason: 在矩阵重建前统一夹紧最终 second angle 到安全区 `[0,89] U [271,360)`，避免 second angle 穿过 `90` 度后的视角翻面

## Reserved Hooks
- `PAL4_Main_WndProc @ 0x40A170`
  - mode: reserved
  - patch span: `8`
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
- `inject/` 只消费这些常量，不把 `rebuild/src` 当实现依赖。

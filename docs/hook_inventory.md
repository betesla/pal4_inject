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

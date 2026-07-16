# PAL4 Inject Launcher 使用说明

双击 `PAL4_inject.exe` 会打开中文 launcher。所有常用配置都在游戏启动前保存，runtime DLL 不再创建游戏内控制面板。

## 游戏设置

- `CS 文本脚本`：适合脚本调试和快速迭代。
- `CSB 原始脚本`：适合普通游玩和回归验证，也是默认选项。
- `常用分辨率 / 显示器支持`：选择预设分辨率，也可以直接输入宽高。
- `全屏运行 / 启用宽屏 / 垂直同步`：写入游戏目录的 `config.cfg`。
- 保存 `config.cfg` 前会保留一份 `config.cfg.bak`。

## 注入功能

该页提供面向普通玩家的功能开关，并按“渲染与画面 / 输入与界面 / 脚本与文本 / 相机”分组。

- 关闭功能会把对应 Hook 设为 `observe_only`。
- 再次开启会恢复它记住的活动模式，默认使用 `replace_with_fallback`。
- MSAA 支持 `关闭 / 2x / 4x / 8x`；选择非关闭等级时会同时启用底层 MSAA Hook。
- launcher 保存的是期望配置。显卡或游戏可能拒绝某个 MSAA 等级，实际结果应查看 runtime 日志或 CLI 状态。
- “宽屏界面居中”控制 4:3 UI 逻辑区与白名单菜单黑边遮罩。
- “Bink 视频 4:3 居中”保持过场视频原比例，避免宽屏横向拉伸。
- 战斗浮字、提示窗与结算图片的宽屏修正分成多个 Hook，排查回归时可在高级调试中单独关闭。

## 高级调试

这里可以逐项设置 HookMode 和详细日志：

- `仅观察`：统计调用，不替换游戏行为。
- `镜像比对`：用于实验性对照。
- `替换（可回退）`：优先使用注入实现，必要时回退原逻辑。
- `强制替换`：不回退，主要用于开发验证。
- `日志`：只为选中的功能写入详细运行记录，普通使用建议保持关闭。

## 配置与运行状态

- 新配置文件：`pal4_inject\inject_settings.ini`
- 旧版 `pal4_inject\inject_panel_settings.ini` 会被兼容读取，并在下次从 launcher 启动时迁移到新文件名。
- runtime 会在 Hook 安装前加载配置。
- 游戏运行期间如需改变 HookMode，底层 IPC 协议仍保留 `set_hook_mode`；普通 CLI 主要用于状态和事件日志观察。
- 实际安装状态、调用次数和错误通过以下渠道查看：
  - `pal4_inject\pal4_inject_runtime.log`
  - `cli.exe --pid <pid> state`
  - `cli.exe --pid <pid> event-log`

## 故障排查

- launcher 无法初始化界面：确认系统存在 DirectX 9 runtime 和可用显卡驱动。
- 中文退回默认字体：确认 `C:\Windows\Fonts\msyh.ttc` 存在。
- 配置无法保存：确认游戏目录和 `pal4_inject` 子目录可写。
- 某项配置无效果：先检查 runtime 日志是否明确记录 Hook 安装成功，再判断游戏行为。

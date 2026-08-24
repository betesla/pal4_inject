# PAL4 Inject Launcher 使用说明

双击 `PAL4_inject.exe` 会打开中文 launcher。所有常用配置都在游戏启动前保存，runtime DLL 不再创建游戏内控制面板。

## 游戏设置

- `CS 文本脚本`：适合脚本调试和快速迭代。
- `CSB 原始脚本`：适合普通游玩和回归验证，首次启动默认选中。
- launcher 会把上次启动时选择的 `CS / CSB` 写入 `inject_settings.ini`，下次打开自动恢复。
- `常用分辨率 / 显示器支持`：选择预设分辨率，也可以直接输入宽高。
- `显示模式`提供三种选择：
  - `普通窗口`：使用原游戏的窗口边框。
  - `无边框窗口（推荐）`：D3D9 保持窗口呈现，窗口覆盖目标显示器；选择该模式时会同步当前桌面分辨率，之后仍可手动调整。它可减少启动、Alt+Tab 和返回桌面时由显示模式切换造成的黑屏。
  - `独占全屏`：保留原游戏全屏行为，作为兼容性回退。
- `启用宽屏 / 垂直同步`：写入游戏目录的 `config.cfg`。
- 保存 `config.cfg` 前会保留一份 `config.cfg.bak`。

## 增强功能

该页只保留面向普通玩家的稳定选项，不再逐条展示底层 Hook。

- 游戏设置中的“启用宽屏”是宽屏修正总开关，会统一启停：
  - 4:3 UI 居中与菜单黑边
  - 动态字体重同步
  - 小地图与 HUD 布局修正
  - 战斗浮字、提示窗与结算图片修正
  - Bink 过场视频比例修正
- MSAA 支持 `关闭 / 2x / 4x / 8x`；选择非关闭等级时会同时启用底层 MSAA Hook。
- launcher 保存的是期望配置。显卡或游戏可能拒绝某个 MSAA 等级，实际结果应查看 runtime 日志或 CLI 状态。
- “过场视频显示”提供两种保持原比例的模式：
  - `完整显示（保持全画面）`：整个视频都保留，宽屏下允许左右留黑。
  - `宽屏铺满（上下裁剪）`：优先铺满屏幕宽度，保持比例并裁掉超出屏幕的上下画面。
- 相机俯仰保护仍保留独立开关，它不属于宽屏预设。
- “松散文件补丁”是独立开关。启用时，CPK 资源与 CS 文本脚本都优先读取 `gamepatch\gamedata\...`；CPK 未命中仍可回退原 CPK，但 CS 脚本缺失或读取失败会直接失败，不会回退游戏目录 `editData`。关闭开关才恢复原版直读路径；发行版没有 `editData` 时，CS 模式因此不可用。两种状态都会记录到独立加载日志。

## 高级调试

这里可以逐项设置 HookMode 和详细日志：

- 宽屏预设管理的 Hook 仍保留在表格中，便于对当次启动做临时回归排查；下次打开 launcher 时会再次按“启用宽屏”同步。

- `仅观察`：统计调用，不替换游戏行为。
- `镜像比对`：用于实验性对照。
- `替换（可回退）`：优先使用注入实现，必要时回退原逻辑。
- `强制替换`：不回退，主要用于开发验证。
- `日志`：只为选中的功能写入详细运行记录，普通使用建议保持关闭。
- 松散文件补丁固定使用 `gamepatch\loose_file_load.log`，所以高级表格显示“独立文件”，不受通用详细日志复选框控制。

## 反馈 Bug

- launcher 会检查 `pal4_inject` 子目录中的最新崩溃文本，并在侧栏显示“发现崩溃”。
- 可填写 Issue 标题、问题现象和复现步骤，并选择是否附带：
  - 最新崩溃文本
  - runtime log 末尾
- 选中的诊断文本会先在本地脱敏并显示完整预览。游戏目录、Windows 用户目录和常见令牌/密码字段不会按原值带入正文。
- 只要选择了任一诊断项，就必须额外勾选授权确认，按钮才会启用。
- 点击“复制内容并打开 Gitee”后，launcher 会打开预填的 Gitee 新建 Issue 页面，并将完整正文复制到剪贴板。请在网页中再次检查并手动提交；URL 过长导致预填截断时可直接粘贴剪贴板内容。
- 第一版不会读取或上传 `.dmp`，也不会在后台静默创建 Issue。

## 配置与运行状态

- 新配置文件：`pal4_inject\inject_settings.ini`
- 脚本选择使用 `script_mode=cs|csb` 持久化；旧配置缺省该项时使用 `csb`。
- Bink 显示模式使用 `bink_scaling_mode=fit|fill_width_crop` 持久化；旧配置缺省该项时使用“完整显示”。
- 无边框窗口使用 `borderless_window=0|1` 持久化；旧配置缺省该项时沿用 `config.cfg` 的普通窗口或独占全屏设置。
- 旧版 `pal4_inject\inject_panel_settings.ini` 会被兼容读取，并在下次从 launcher 启动时迁移到新文件名。
- runtime 会在 Hook 安装前加载配置。
- 游戏运行期间如需改变 HookMode，底层 IPC 协议仍保留 `set_hook_mode`；普通 CLI 主要用于状态和事件日志观察。
- 实际安装状态、调用次数和错误通过以下渠道查看：
  - `pal4_inject\pal4_inject_runtime.log`
  - `gamepatch\loose_file_load.log`（CPK / CS 松散文件覆盖、CS 必需文件缺失与 CPK 回退，UTF-8 JSON Lines）
  - `cli.exe --pid <pid> state`
  - `cli.exe --pid <pid> event-log`
- `state` 中的 `borderless_window_enabled` 表示期望配置，`borderless_window_applied` 和 `borderless_window_summary` 表示本次实际应用结果。

## 故障排查

- launcher 无法初始化界面：确认系统存在 DirectX 9 runtime 和可用显卡驱动。
- 中文退回默认字体：确认 `C:\Windows\Fonts\msyh.ttc` 存在。
- 配置无法保存：确认游戏目录和 `pal4_inject` 子目录可写。
- 某项配置无效果：先检查 runtime 日志是否明确记录 Hook 安装成功，再判断游戏行为。

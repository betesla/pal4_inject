# 启动器自动更新

## 用户流程

启动器打开后立即显示界面，并在工作线程中查询 Gitee 和 GitHub 的最新正式 Release。只有版本高于当前版本、具有合法 `update.json` 且对应更新 ZIP 已存在时，版本号旁才会显示更新箭头。离线、服务超时和旧发布缺少清单不会弹窗。

点击箭头显示版本说明；点击“更新并重启”才开始下载。下载、校验或等待游戏退出期间可以取消。检测到本安装目录的 `PAL4.exe` / `launch.exe` 时会等待玩家保存并自行退出，不结束游戏进程。准备完成后保存当前启动器设置，退出启动器，由独立 helper 安装并重新打开启动器，不自动进入游戏。

稳定版使用 `vMAJOR.MINOR.PATCH` 数字比较，忽略草稿、预发布和非标准版本号。镜像不同步时优先选择具有合法清单的较新版本，不允许降级。两边清单声明同版本、同大小、同 SHA256 的更新包时，可在下载失败后切换发布源。

## 文件所有权与校验

- 必需文件：`PAL4.exe`、`PAL4Plus.exe`、`pal4_inject/runtime.dll`、`pal4_inject/cli.exe`，必须成套更新。
- schema 1 额外托管文件：`pal4_inject/rtx_remix_compatibility.conf`。当前标准发布脚本不打包此可选文件。
- 随包更新 `pal4_inject/THIRD_PARTY_NOTICES.txt`，包含 JSON 与 ImGui 的完整许可。
- 允许清理已停用的 `PAL4_inject.exe`。远程清单不能指定白名单以外的覆盖或删除路径。
- `PAL4.exe` 随包更新并参与备份和回滚。不更新 `launch.exe`、存档、`config.cfg`、注入设置、日志或 MOD。安装事务不会改动它们；启动器会按原设置保存流程保存用户本次更改。
- 对压缩包和每个解压文件验证长度与 SHA256。拒绝多余、重复、路径穿越、链接及超限文件。压缩包上限 256 MiB，解压内容总上限 512 MiB。
- 所有下载使用 HTTPS 和系统证书验证，禁止重定向到 HTTP。SHA256 用于内容完整性校验；本实现没有离线签名或 Authenticode 验签，信任官方 Release 账户及 HTTPS。

## 发布

正常使用 `scripts/release.ps1`。脚本要求 `dist/PAL4.exe` 存在并在刷新目录时保留它，再由 `scripts/update-package.ps1` 从明确列表一次性生成完整 ZIP 和清单。手动安装与自动更新使用同一个包含 `PAL4.exe` 的 ZIP。

每个 Release 上传两个附件：

1. `PAL4Plus_vX.Y.Z_win32.zip`：含 `PAL4.exe`、增强启动器、运行库、CLI 和第三方许可的统一安装包。
2. `update.json`：最后上传，包含 schema、平台、稳定渠道、版本、说明、ZIP 名称/大小/哈希、逐文件大小/哈希和删除清单。

本地验证且不发布：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/release.ps1 -SkipGitHubRelease -SkipGiteeRelease
```

统一 ZIP 输出到仓库根目录，清单输出到 `<BuildDir>/update-release`。首次需要手动安装一个包含此功能的版本；旧版启动器无法凭空获得自动替换能力。之后的发布必须携带更新附件，并提升 `CMakeLists.txt` 中的版本号。仅修改构建号不触发更新。

v0.2.4 已改为统一包格式。此前双包版本的启动器不支持此格式，已安装该构建的用户需要重新下载当前完整包覆盖一次；版本号相同不会触发自动更新。

## 事务与恢复

准备目录为游戏目录内的 `.pal4plus-update/{GUID}`，与目标文件同盘。该目录保存下载包、解压文件、旧文件备份和 JSON 日志。helper 使用独占文件句柄锁住安装目录；只有原启动器退出且游戏不在运行时才替换文件。

先完成全部备份并持久化 journal / pending 指针，再以临时文件和 `MoveFileEx` 逐个替换，启动器最后替换。单文件替换是原子的，多文件事务依赖恢复日志。新版启动器完成设置读取和首帧界面构建后发送命名事件确认；helper 等待最多 90 秒。新版提前退出或未确认时，helper 仅结束自己启动的新版启动器并恢复旧文件。提交后清理失败不触发回滚，后续启动继续清理残余文件。

安装过程中意外退出或重启：下次打开启动器若发现未提交日志，会启动恢复 helper 并立即退出，由 helper 恢复备份再打开旧版。恢复重复执行仍有效。若目录不可写、文件被其他进程占用或备份受损，保留日志和备份并报告错误，不删除存档。若启动器本身已损坏而无法运行，需要手动解压完整安装包修复入口。

运行依赖：Windows WinHTTP / BCrypt，以及系统自带 Windows PowerShell 5.1 和 .NET ZIP 支持。网络采用系统代理配置，不把开发机代理端口写入程序。没有管理员权限自动提升，也不修改系统代理。

## 验证

`pal4_update_tests` 在随机临时目录执行，不使用真实游戏目录。覆盖版本比较、两类 Release JSON、清单白名单、哈希、中文/空格路径、实际发布脚本生成的 ZIP、恶意 ZIP、安装/回滚、锁定文件、新文件撤销、配置与存档保留、helper 启动确认、新版失败恢复、中断恢复、游戏占用，以及离线网络/镜像/重试策略。

```powershell
cmake -S . -B build -A Win32 -DPAL4_INJECT_ENABLE_DEV_PUBLISH=OFF
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

线上发布后另行验证实际客户端的发现、下载、解压与哈希校验；默认测试不创建或修改远程 Release。

# 松散文件优先补丁

## 用途

新增或调整关卡资源、CS 文本脚本时，不再要求每次重新打包 CPK。runtime 会在 CPK 资源入口和 CS 文本解释器的直接文件入口检查同一个专用补丁目录：

```text
<游戏目录>\gamepatch\<游戏内完整资源路径>
```

松散文件存在且能正常读取时优先使用。不存在时，普通资源继续走原版 CPK；CS 文本脚本直接加载失败，不再读取游戏目录下的 `editData`。发行版原本就不携带这套文本源码，CS 模式所需脚本必须完整放入 `gamepatch`。原版 CPK 资源无需解包。

## 目录示例

如果游戏请求：

```text
gamedata\PALWorld\Q99\Q99\A.dff
```

补丁文件应放到：

```text
<游戏目录>\gamepatch\gamedata\PALWorld\Q99\Q99\A.dff
```

路径从 `gamedata` 开始完整保留。关卡目录中关联的 `.bsp`、`.dff`、贴图、场景数据和脚本分别按游戏实际请求路径放置；不需要把整个 CPK 解包到补丁目录。

CS 模式下，例如游戏直接读取：

```text
gamedata\editData\script\M10.cs
```

对应补丁位置是：

```text
<游戏目录>\gamepatch\gamedata\editData\script\M10.cs
```

原游戏有两个已知的扩展名错误：CS 模式会把文本脚本请求写成 `Music.csb` 和 `worldMap.csb`。文本解释器 seam 会把所有误入该入口的 `.csb` 请求映射为同名 `.cs` 源码，因此对应补丁必须命名为：

```text
<游戏目录>\gamepatch\gamedata\editData\script\Music.cs
<游戏目录>\gamepatch\gamedata\editData\script\worldMap.cs
```

不要把文本源码命名成 `.csb`。`.csb` 专用于已经编译的二进制脚本。

## 优先级与安全边界

1. 若专用补丁目录中存在同路径且类型校验通过的普通文件，使用松散文件。
2. 若文件不存在，CPK 资源自动回退 CPK；CS 文本脚本记录 `gamepatch_required_missing` 并直接返回失败。
3. 若松散文件存在但打开、映射或句柄分配失败：
   - CPK 资源在 `替换（可回退）` 下记录错误并回退原 CPK；`强制替换` 下直接失败。
   - CS 文本脚本在两种替换模式下都直接失败，不尝试游戏目录原路径。
4. 只接受 `gamedata\...` 相对路径；绝对路径和包含 `..` 的路径不会进入松散文件层。
5. CPK seam 的空文件和超过 4 GiB 的单文件不会作为覆盖资源加载；CS 文本脚本仍服从原文本解释器的读取结果。
6. CPK seam 对 `.csb` 覆盖额外校验 4 字节小端 payload 长度头。头部与实际文件长度不符时，将该文件视为错误类型并回退原 CPK，避免文本源码被二进制解释器当成巨大数据块。

launcher 的“增强 -> 松散文件补丁”提供一个统一勾选开关，默认启用并使用 `替换（可回退）`。它同时控制 CPK 资源和 CS 文本脚本。取消勾选后恢复原版加载路径；由于发行版没有 `editData`，关闭后 CS 模式通常不能工作。高级页仍可选择完整 HookMode，但两种替换模式对 CS 都执行“仅 gamepatch、无回退”策略。

## 热更新限制

CPK 资源在打开时使用只读内存映射，并在游戏释放该资源时解除映射。CS 文本脚本会被原解释器一次性读入内存。已经打开或解释的内容不会自动变化；需要让游戏重新装载对应资源/脚本（通常是重新进入关卡或重启游戏）。Windows 共享模式允许编辑器替换补丁文件，但已经打开的旧映射不会自动变成新内容。

## 独立加载日志

松散文件功能不使用通用 runtime 详细日志开关。每次资源请求都会追加到：

```text
<游戏目录>\gamepatch\loose_file_load.log
```

文件编码为 UTF-8，每行是一个独立 JSON 对象，其他软件可以边运行边读取。主要事件：

- `session_start`：新游戏进程开始记录，包含当前模式、`gamepatch` 根目录与格式版本。
- `override`：已使用松散文件，包含实际文件和大小。
- `cpk_fallback`：CPK seam 未找到可用松散文件，并记录 CPK 是否成功打开；错误命名或损坏的 `.csb` 会在 `reason` 中记录 `invalid_csb_header`。
- `gamepatch_required_missing`：CS seam 未找到必需的 `gamepatch` 脚本，直接返回失败且没有 fallback 字段。
- `overlay_disabled`：UI 开关已关闭，记录被忽略的候选文件以及原加载器结果。
- `mirror_compare`：高级页使用镜像比对模式，不替换资源。
- `override_error`：松散文件打开、映射、句柄分配或脚本读取失败；CPK 可按模式回退，CS 不回退。

格式版本 3 的关键字段如下；相较版本 2，启用状态下的 CS 缺失/读取失败不再产生任何回退字段：

- `loader`：`package` 或 `text_script`，用于区分 CPK 资源入口与 CS 文本脚本入口。
- `fallback_source`：发生回退时的原加载源；正常启用状态下只有 CPK 使用 `cpk`。`game_directory` 只可能出现在关闭功能或镜像观察状态。
- `fallback_opened`：原加载器是否成功打开。
- `fallback_used`：本次请求是否实际使用了原加载器。
- `cpk_opened / fallback_to_cpk`：仅在 `fallback_source=cpk` 时保留的兼容字段，旧排查工具可以继续读取。

示例：

```json
{"time_utc":"2026-07-17T04:03:04.125Z","pid":63268,"event":"override","loader":"package","mode":"replace_with_fallback","resource":"gamedata\\PALActor\\101\\101_2.png","file":"I:\\Games\\PAL4\\gamepatch\\gamedata\\PALActor\\101\\101_2.png","size":490778}
{"time_utc":"2026-07-17T04:03:04.126Z","pid":63268,"event":"gamepatch_required_missing","loader":"text_script","mode":"replace_with_fallback","resource":"gamedata\\editData\\script\\M10.cs","file":"I:\\Games\\PAL4\\gamepatch\\gamedata\\editData\\script\\M10.cs","reason":"gamepatch_file_required"}
```

日志达到 8 MiB 后，会在下一次游戏启动时轮换为 `loose_file_load.log.1`。如果资源未按预期覆盖，优先检查 `event`、`loader`、`resource`、`file` 和 `reason` 字段；CS 记录出现 fallback 字段只表示功能已关闭或处于镜像观察模式。

## 实现依据

- CPK 资源入口：`PackageResourceManager_OpenFile @ 0x66E820`（原 IDA 名 `ScriptManager_LoadScriptFile_2`）。
- CS 文本脚本入口：`cs_TextScriptInterpreter_Initialize @ 0x7E0DA0`。该函数直接 `CRT_Fopen(file_name, "rb")`，因此需要单独的内部 hook；启用时只把 `gamepatch` 绝对路径交给原读取器，缺失/失败不再用原相对路径调用它。进入该文本入口的 `.csb` 误命名请求会在候选路径层改为 `.cs`，不会修改 CSB 模式仍需使用的原文件名常量。该内部 hook 跟随同一个 `loose_file_overlay` UI 开关，不对用户暴露第二个功能项。
- runtime 复用原 CPK manager 的 8 个文件句柄槽和内存映射句柄布局。
- 原版 `Package_ReadData / Package_Seek / sub_793C10(close)` 无需额外 detour；松散文件释放仍沿用原版生命周期。

# PAL4 Inject v0.1.8

## 更新内容

- 修复 4K 分辨率下战斗伤害数字位置漂移、缩放过头的问题。
- 收口宽屏 UI 坐标换算逻辑，战斗浮字按统一的 active UI viewport plan 映射，避免不同分辨率下重复缩放。
- 修正发布布局，`runtime.dll` / `cli.exe` 严格放在 `pal4_inject` 子目录，`PAL4_inject.exe` 保持在游戏根目录。

## 鸣谢

特别感谢 **Agaricus sinodeliciosus** 提供 token，支持本次调试与发布。

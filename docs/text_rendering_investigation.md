# PAL4 Text Rendering Investigation

## Scope
这份记录专门沉淀 PAL4 注入轨里对“宽屏 UI 下文本发糊”的调查链路、当前结论和后续候选修复方向。

## Problem Statement
- 当前 inject 宽屏方案已经把 UI 改为居中 4:3 逻辑区域显示。
- 但在 `1600x900 / 1920x1080` 等宽屏分辨率下，文本仍明显比原始 4:3 显示更糊。
- 这个问题与 `MSAA` 无关，主要表现为：
  - UI 框体位置正确
  - 文本边缘和笔画发虚
  - 尤其是小字号动态字体更明显

## Inject-Side Call Chain

### 1. Current widescreen render patch
- 当前宽屏 patch 位于：
  - `inject/src/runtime/cegui_renderer_hooks.cpp`
- 关键 seam：
  - `CEGUI_Renderer_Constructor_2 @ 0x413580`
- 当前 patch 机制：
  - 通过 object-local synthetic vtable 接管 renderer 对象的 `doRender` / `getRenderRect`
  - 在 `Hook_CeguiRendererDoRenderWide(...)` 里对所有 queued rect 顶点统一应用：
    - `uniform_scale`
    - `horizontal_bias_pixels`
- 当前关键结论：
  - 这条链并不区分“文字 glyph quad”和“普通 UI quad”
  - 所有 CEGUI 几何都会被统一缩放

### 2. Current render math that can affect text sharpness
- 当前宽屏 patch 使用的 renderer 内部缩放槽：
  - `renderer + 0x110` -> `scale_x`
  - `renderer + 0x114` -> `scale_y`
- 当前 hook 里额外做过一版未完全验证的像素对齐试验：
  - 把缩放后的顶点重新对齐到 D3D9 half-pixel 网格
  - 目的：
    - 减少动态字体 glyph quad 在非整数统一缩放下落到坏采样位置
  - 当前状态：
    - 已编译进入实验 DLL
    - 还没有足够的运行时视觉证据证明它单独就能彻底解决文本模糊

## Original EXE Font Load Chain

### 1. IDA-backed load path
- `InitializeUIFrameManager @ 0x4BDEF0`
  - xref 到 `LoadFontFile @ 0x4BD3B0`
- `LoadFontFile @ 0x4BD3B0`
  - 最终调用：
    - `CEGUI::FontManager::getSingleton`
    - `CEGUI::FontManager::createFont(...)`
- 当前高置信度语义：
  - PAL4 不是简单把字体图片贴上去
  - 至少对主界面文字字体，原始 EXE 是通过 CEGUI dynamic font 路径创建字体

### 2. Real game font definitions
来自真实游戏资源：
- `I:\Games\PAL4_game\gamedata\decompressedData\ui\fonts\system.font`
- `I:\Games\PAL4_game\gamedata\decompressedData\ui\fonts\SystemBold.font`
- `I:\Games\PAL4_game\gamedata\decompressedData\ui\fonts\Dialog_SIMSUN.font`

当前已证实：
- `system.font`
  - `Name="system"`
  - `Filename="gamedata\ui\fonts\simsun.ttc"`
  - `Type="Dynamic"`
  - `Size="13"`
  - `NativeHorzRes="800"`
  - `NativeVertRes="600"`
  - `AutoScaled="true"`
  - `AntiAlias="true"`
- `SystemBold.font`
  - `Name="systemBold"`
  - `Filename="gamedata\ui\fonts\SystemBold.ttf"`
  - `Type="Dynamic"`
  - `Size="13"`
  - `NativeHorzRes="800"`
  - `NativeVertRes="600"`
  - `AutoScaled="true"`
  - `AntiAlias="true"`
- `Dialog_SIMSUN.font`
  - `Name="dialog_simsun"`
  - `Filename="gamedata\ui\fonts\kai.ttf"`
  - `Type="Dynamic"`
  - `Size="20"`
  - `NativeHorzRes="800"`
  - `NativeVertRes="600"`
  - `AutoScaled="true"`
  - `AntiAlias="true"`

这意味着：
- 当前主 UI 文字发糊，不能简单解释为“PAL4 原字体没开抗锯齿”
- 至少主界面和对话常用字体本来就走 dynamic font + autoscale 路径

## Upstream CEGUI 0.4.1 Cross-Check
参考来源：
- `third_party/cegui-0.4.1/src/cegui_mk2/src/CEGUIFont.cpp`
- `third_party/cegui-0.4.1/src/cegui_mk2/src/CEGUIFont_xmlHandler.cpp`
- `third_party/cegui-0.4.1/src/cegui_mk2/include/CEGUIFont.h`

当前已确认的上游行为：
- `Font_xmlHandler` 会从 `.font` 读取：
  - `Size`
  - `NativeHorzRes`
  - `NativeVertRes`
  - `AutoScaled`
  - `AntiAlias`
- `Font::notifyScreenResolution(...)`
  - 会更新 `d_horzScaling / d_vertScaling`
  - 当 `d_autoScale=true` 时会调用 `updateFontScaling()`
- `Font::updateFontScaling()`
  - 对 dynamic font 会继续调用 `createFontFromFT_Face(...)`
- `Font::createFontFromFT_Face(...)`
  - 当 `d_autoScale=true` 时，会按当前屏幕 DPI 和 scaling 重新生成 glyph atlas
- `Font::drawText(...)`
  - 最终还支持额外的 `x_scale / y_scale`

这条参考链支持一个很重要的判断：
- CEGUI dynamic font 自己已经有“随分辨率重建 glyph atlas”的设计
- 如果 PAL4 当前文字仍然糊，问题更可能出在 inject 的 renderer 级二次缩放，而不是原字体定义完全缺失高分辨率路径

## Runtime Export Surface Confirmed In `CEGUIBase.dll`
通过本地导出检查，当前已确认 `CEGUIBase.dll` 导出这些与字体相关的方法：
- `?getSingleton@FontManager@CEGUI@@SAAAV12@XZ`
- `?getSingletonPtr@FontManager@CEGUI@@SAPAV12@XZ`
- `?createFont@FontManager@CEGUI@@QAEPAVFont@2@ABVString@2@0@Z`
- `?getFont@FontManager@CEGUI@@QBEPAVFont@2@ABVString@2@@Z`
- `?notifyScreenResolution@Font@CEGUI@@QAEXABVSize@2@@Z`
- `?setAutoScalingEnabled@Font@CEGUI@@QAEX_N@Z`
- `?setNativeResolution@Font@CEGUI@@QAEXABVSize@2@@Z`
- `?getPointSize@Font@CEGUI@@QBEIXZ`

这意味着：
- 从 inject 轨做“运行时重新通知字体分辨率 / 重新设 native resolution / 重开 autoscale”的实验修复是现实可行的
- 后续不一定非要通过更重的全局 renderer hack 才能处理文字清晰度

## Runtime Probe Lesson
- 2026-04-09 做过一版“在 `CEGUI_System_Initialize` 后立即枚举已知字体对象”的运行时诊断实验：
  - 通过 `FontManager::getFont`
  - `Font::isAutoScaled`
  - `Font::getPointSize`
  - `Font::getFontHeight`
- 当前结论：
  - 这条探针接入时机过早，曾触发 `abort(0)` / 非正常终止
  - 已从稳定运行时路径中移除
- 经验：
  - 不要在 `CEGUI_System_Initialize` 返回后的第一时间假设目标字体一定已经完成注册
  - 后续如需运行时拿到字体对象，应优先转向更晚、更窄的 seam，例如：
    - `LoadFontFile @ 0x4BD3B0`
    - 或字体创建之后的更晚期调用链

## Current Working Hypothesis
当前最可信的解释是：

1. PAL4 的 dynamic font 本来已经按 `800x600` native resolution + autoscale 设计。
2. inject 宽屏 patch 把中央 4:3 UI 区整体按 `uniform_scale` 放大。
3. 文本 glyph quad 和普通 UI quad 一起被二次非整数缩放。
4. 因为 glyph atlas 本身是 texture，二次缩放后的采样位置不再稳定对齐像素网格，于是文字发糊。

简化结论：
- 当前模糊更像是“renderer 级二次缩放问题”
- 不是“主字体本身没开 AntiAlias”

## Candidate Fix Directions

### Direction A: keep current renderer patch, improve pixel alignment
- 方案：
  - 在 inject 的 `Hook_CeguiRendererDoRenderWide(...)` 里进一步优化缩放后顶点对齐
- 优点：
  - 风险最小
  - 不需要动字体对象生命周期
- 缺点：
  - 只能缓解由采样位置造成的模糊
  - 如果 glyph atlas 分辨率本身仍不足，效果有限

### Direction B: re-notify dynamic fonts using centered logical UI resolution
- 方案：
  - 运行时获取 `system / systemBold / dialog_simsun`
  - 通过 `Font::setNativeResolution(...)` / `Font::notifyScreenResolution(...)` 按“中央有效 UI 尺寸”而不是整屏宽度重算
- 优点：
  - 更接近根治 dynamic font 清晰度问题
  - 直接利用 CEGUI 已有 autoscale 机制
- 风险：
  - 需要谨慎选择触发时机，避免和原始 font manager 生命周期冲突

### Direction C: special-case text quads inside renderer patch
- 方案：
  - 在 renderer wide patch 里识别“文字 glyph texture”并避免再次统一放大
- 优点：
  - 理论上最直接命中当前症状
- 风险：
  - 识别标准脆弱
  - 更依赖 renderer 内部 layout/texture handle 语义

## Current Status
- 已完成：
  - 字体资源类型、字号、autoscale/AA 配置确认
  - 原始 EXE `LoadFontFile -> FontManager::createFont` 链确认
  - 上游 CEGUI dynamic font autoscale 机制交叉验证
  - `CEGUIBase.dll` 字体相关导出确认
- 当前未完成：
  - 还没有最终 runtime patch 证明哪一种修复对 PAL4 当前文本模糊最有效
- 当前建议优先级：
  1. 继续验证 renderer half-pixel alignment 试验
  2. 若改善不足，优先做 dynamic font `notifyScreenResolution / setNativeResolution` 方向的运行时实验

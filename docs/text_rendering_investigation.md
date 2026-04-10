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

## IDA Evidence From PAL4 `CEGUIBase.dll`
已在 IDA 中直接确认 PAL4 当前使用的 `CEGUIBase.dll`，目标文件为：
- `I:\Games\PAL4_game\CEGUIBase.dll`

当前已在真实 DLL 中确认这些函数：
- `Font::notifyScreenResolution @ 0x100B6D40`
- `Font::updateFontScaling @ 0x100B6E30`
- `Font::createFontFromFT_Face @ 0x100B6F10`
- `Imageset::notifyScreenResolution @ 0x100BFB10`

关键反编译结论：
- `Font::notifyScreenResolution(...)`
  - 会把 `this+96 / this+97` 更新为：
    - `screen_width / native_horz_res`
    - `screen_height / native_vert_res`
  - 然后在 auto-scale 打开时调用 `Font::updateFontScaling()`
- `Font::updateFontScaling()`
  - 对 FreeType / dynamic font 路径，会读取 renderer 当前 DPI
  - 再调用 `Font::createFontFromFT_Face(this, ptSize, horzDpi, vertDpi)`
- `Font::createFontFromFT_Face(...)`
  - 在 auto-scale 打开时，会先把传入 DPI 乘上：
    - `d_horzScaling`
    - `d_vertScaling`
  - 然后再进 `FT_Set_Char_Size(...)`
- `Imageset::notifyScreenResolution(...)`
  - 会更新 `d_horzScaling / d_vertScaling`
  - 然后调用 `Imageset::updateImageScalingFactors()`

这说明：
- 你怀疑的“内部动态图集参数可能仍然在用 800x600”是有根据的
- 更精确地说：
  - PAL4 当前 DLL 的 dynamic font 缩放分母确实来自 `native_horz_res / native_vert_res`
  - 如果 native 仍是 `800 / 600`，那后续 atlas 重建会围绕这个基准算出 `d_horzScaling / d_vertScaling`
  - 对 `1920x1080`，如果 native 维持 `800x600`，那么这里算出来的就是：
    - `d_horzScaling = 1440 / 800 = 1.8`
    - `d_vertScaling = 1080 / 600 = 1.8`

简化结论：
- PAL4 真实 `CEGUIBase.dll` 里，dynamic font atlas 重建并不是“完全无视分辨率”
- 但它确实是“以 native resolution 为分母”来算缩放
- 因此如果我们刻意把 native 保持在 `800x600`，那 atlas 仍然是在“800x600 基准”上推导出的放大版本，而不是切换到一个新的更宽 native 基准

## Runtime Export Surface Confirmed In `CEGUIBase.dll`
通过本地导出检查，当前已确认 `CEGUIBase.dll` 导出这些与字体相关的方法：
- `?getSingleton@FontManager@CEGUI@@SAAAV12@XZ`
- `?getSingletonPtr@FontManager@CEGUI@@SAPAV12@XZ`
- `?getSingletonPtr@ImagesetManager@CEGUI@@SAPAV12@XZ`
- `?createFont@FontManager@CEGUI@@QAEPAVFont@2@ABVString@2@0@Z`
- `?getFont@FontManager@CEGUI@@QBEPAVFont@2@ABVString@2@@Z`
- `?getImageset@ImagesetManager@CEGUI@@QBEPAVImageset@2@ABVString@2@@Z`
- `?getTexture@Imageset@CEGUI@@QBEPAVTexture@2@XZ`
- `?notifyScreenResolution@Font@CEGUI@@QAEXABVSize@2@@Z`
- `?setAutoScalingEnabled@Font@CEGUI@@QAEX_N@Z`
- `?setNativeResolution@Font@CEGUI@@QAEXABVSize@2@@Z`
- `?getPointSize@Font@CEGUI@@QBEIXZ`

这意味着：
- 从 inject 轨做“运行时重新通知字体分辨率 / 重新设 native resolution / 重开 autoscale”的实验修复是现实可行的
- 也可以从 inject 轨拿到 dynamic font 对应的 `Imageset / Texture`，从 renderer 层对字体 atlas 做更精确的专项处理

## Runtime Evidence Confirmed In Current Inject Build
- 当前运行时日志已经证实 `LoadFontFile` seam 能稳定命中已知 dynamic UI 字体，并成功执行重同步：
  - `systemBold ... action=resynced native=800x600 notify=1440x1080`
  - `system ... action=resynced native=800x600 notify=1440x1080`
  - `dialog_simsun ... action=resynced native=800x600 notify=1440x1080`
- 这说明：
  - `LoadFontFile` hook 已经成功安装
  - `FontManager::getFont` / `setAutoScalingEnabled` / `setNativeResolution` / `notifyScreenResolution` 这条 runtime 后处理链已经真实跑过
  - 当前“对白仍然模糊”已经不能再归因于“字体重同步根本没生效”

## Binary Evidence Confirmed For `LoadFontFile`
- 当前 `PAL4.exe` 与 `launch.exe` 在 `LoadFontFile @ 0x4BD3B0` 的真实起始字节已重新确认：
  - `6A FF 68 99 37 82 00 64 A1 00 00 00 00 50 64 89`
- 之前 inject 轨曾把这条 seam 的 `expected_prologue` 里的 SEH 立即数写成旧值，导致：
  - `prologue mismatch for load_font_file`
  - 文本修复 hook 未安装
- 该字节证据已用于修正 hook metadata。

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

## Upstream Source Evidence For Font Atlas Ownership
参考来源：
- `third_party/cegui-0.4.1/src/cegui_mk2/include/CEGUIFont.h`
- `third_party/cegui-0.4.1/src/cegui_mk2/src/CEGUIFont.cpp`
- `third_party/cegui-0.4.1/src/cegui_mk2/src/CEGUIImage.cpp`
- `third_party/cegui-0.4.1/src/cegui_mk2/src/CEGUIImageset.cpp`

当前已确认的上游结构与行为：
- `Font` 内部有：
  - `Imageset* d_glyph_images`
- dynamic font 构造时会创建：
  - `d_glyph_images = ImagesetManager::createImageset(name + "_auto_glyph_images", System::getSingleton().getRenderer()->createTexture())`
- `defineFontGlyphs_impl()` 最终会：
  - `d_glyph_images->undefineAllImages()`
  - `createFontGlyphSet(...)`
  - `d_glyph_images->getTexture()->loadFromMemory(...)`
- 这意味着 dynamic font glyph atlas 的 imageset 命名规则是：
  - `fontName + "_auto_glyph_images"`
  - 例如：
    - `system_auto_glyph_images`
    - `systemBold_auto_glyph_images`
    - `dialog_simsun_auto_glyph_images`

这条链提供了一个可执行的 renderer 侧 seam：
- 可以通过 `ImagesetManager::getImageset` 和 `Imageset::getTexture` 拿到已知动态字体的 atlas texture
- renderer 可以据此精确识别“当前 draw 的是不是字体贴图”

额外运行时经验：
- `ImagesetManager::getImageset(...)` 在目标 imageset 不存在时会抛 `UnknownObjectException`
- 不应把它当成“返回 null 的探针函数”直接在早期路径里裸调
- inject 轨后续若继续沿 `imageset / texture` 路线实验，必须优先走：
  - `isImagesetPresent(...)`
  - 再 `getImageset(...)`

## Upstream Source Evidence For Potential Double Scaling
当前已确认的上游绘制链：
- `Imageset::notifyScreenResolution(...)`
  - 会更新：
    - `d_horzScaling`
    - `d_vertScaling`
- `Imageset::defineImage(...)`
  - 会把当前 imageset 的缩放因子灌入 `Image`
- `Image::getWidth()` / `Image::getHeight()`
  - 返回的是：
    - `d_scaledWidth`
    - `d_scaledHeight`
- `Font::drawTextLine(...)`
  - 会读取：
    - `img->getWidth()`
    - `img->getHeight()`
  - 再构造：
    - `Size(img->getWidth() * x_scale, img->getHeight() * y_scale)`
  - 然后调用：
    - `img->draw(cur_pos, sz, clip_rect, colours)`

这条源码链支持一个更强的判断：
- dynamic font glyph image 本身已经吃过 `Imageset` 级缩放
- `Font::drawTextLine(...)` 又会再乘一遍 `x_scale / y_scale`
- inject 宽屏 patch 目前还会在 renderer 层对最终 queued rect 再统一乘一次 `uniform_scale`

因此当前对白模糊更像是：
1. dynamic font atlas 和 glyph image 先按分辨率做了字体自己的缩放
2. `Font::drawTextLine(...)` 再按文本绘制参数做一次 size 计算
3. inject 的 widescreen renderer patch 最后对所有 quad 再做统一缩放

简化结论：
- 当前问题已经不只是“字体 atlas 分辨率不够”
- 更可能是“字体 quad 在 CEGUI 自己缩放之后，又被 inject renderer 二次统一缩放”

## Failed Experiment Notes
- 2026-04-09 做过一版“已知 dynamic font atlas texture 单独 sampler 处理”的运行时实验：
  - 目标是让字体贴图不再和普通 UI 贴图走同一套采样策略
- 当前结论：
  - 直接从现有 renderer seam 中推断底层 D3D texture / sampler 语义还不够稳定
  - 这版实验曾导致 `PAL4.exe` 在启动后很快异常退出
  - 已回退该试验代码，保留 build id 与证据文档，但不把它继续留在默认 DLL 里
- 经验：
  - 先确认“renderer queued rect 的 texture_handle / texture_stage 到底分别指向哪层对象”
  - 在没有确定对象语义前，不要直接把它当成 D3D COM texture 指针调用方法
- 2026-04-10 做过一版“只对 `dialog_simsun` 把 runtime `native resolution` 从 `800x600` 提高到 `1440x1080`”的 A/B 实验：
  - 结果：
    - 进入 renderer 的 `raw_rect` 没变
    - `uv_rect` 也没变
    - 屏幕观感仍然模糊
  - 当前结论：
    - 这条实验已经被证伪，不应继续保留为默认代码路径
    - 问题不只是 `dialog_simsun` atlas 仍按 `800x600` 基准重建

## Current Working Hypothesis
当前最可信的解释是：

1. PAL4 的 dynamic font 本来已经按 `800x600` native resolution + autoscale 设计。
2. inject 宽屏 patch 把中央 4:3 UI 区整体按 `uniform_scale` 放大。
3. 文本 glyph quad 和普通 UI quad 一起被二次非整数缩放。
4. 因为 glyph atlas 本身是 texture，二次缩放后的采样位置不再稳定对齐像素网格，于是文字发糊。

简化结论：
- 当前模糊更像是“renderer 级二次缩放问题”
- 不是“主字体本身没开 AntiAlias”

## Runtime Evidence For Renderer Scale
- 当前 inject runtime 已经记录到宽屏 renderer 对象在 `1920x1080` 下的原始缩放槽：
  - `original_scale_x=2.4`
  - `original_scale_y=1.8`
- inject 当前的居中 patch 会把这两个槽统一改成：
  - `scale_x=1.8`
  - `scale_y=1.8`
  - 再加 `horizontal_bias_pixels=240`

这说明：
- 原始 PAL4 renderer 默认确实按整屏宽度把 UI 横向拉伸到 `1920 / 800 = 2.4`
- inject 宽屏 patch 是通过把 `x` 方向从 `2.4` 压回 `1.8` 来实现 4:3 居中
- 文字发糊至少已经和这条 renderer 级统一缩放链直接相关

## Runtime Layout Probe Warning
- 2026-04-10 做过一版“按本地 CEGUI 0.4.1 源码布局偏移直接读取 `CEGUI::Font` 内部字段”的实验：
  - 例如尝试读取 `d_glyph_images`
  - 以及推导出的 `d_horzScaling / d_vertScaling / d_nativeHorzRes / d_nativeVertRes`
- 当前结论：
  - PAL4 实际运行时的 `CEGUIBase.dll` 不能安全按 vanilla 0.4.1 的类布局直接硬读
  - 这类探针不仅读不到稳定的有效值，还可能导致运行期崩溃或调试 CRT 报错
- 实践规则：
  - 后续不要再把本地源码推导出的 `CEGUI::Font` 内部偏移直接当成 PAL4 运行时 truth
  - 需要优先依赖：
    - 导出函数
    - 更窄的 seam
    - 或 IDA 里对 PAL4 自己的 `CEGUIBase.dll` 重新确认后的布局证据

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
  - inject 轨已接入 `LoadFontFile` seam，并把 `system / systemBold / dialog_simsun` 的 runtime font resync 做成独立模块
- 当前未完成：
  - 还需要继续做真实游戏视觉验收，确认对话框正文、主菜单和系统说明页在宽屏下的清晰度改善程度
- 当前建议优先级：
  1. 保留 `LoadFontFile` 字体重同步，因为它已经被运行时日志证实真实生效
  2. 继续做 renderer 侧“已知字体 atlas texture / quad 专项处理”
  3. 若专项 texture 路线仍不足，再继续细抠 renderer half-pixel alignment 的残余问题

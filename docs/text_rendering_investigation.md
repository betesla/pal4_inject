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

## 2026-04-22 IDA Breakthrough: RenderWare Texture Filter Path
- 这轮最重要的突破不是继续猜 `*_auto_glyph_images` atlas 名，而是直接把 PAL4 的 CEGUI 渲染链追到了 RenderWare D3D9 状态机。
- 已确认的关键函数：
  - `0x413D80 ProcessAndRenderVertexData`
    - CEGUI queued rect 的实际提交点
    - 这里会在提交前统一走 `dword_950CD0 + 0x20`
  - `0x4149D0 SetRenderStates`
    - CEGUI renderer 默认状态初始化
    - 这里会写入 `state 6 = 0, 7 = 2, 8 = 0, 9 = 2, 12 = 1`
  - `0x759831 Engine_InitializeDevice`
    - 确认 `dword_950CD0` 是 RenderWare 设备接口对象
  - `0x76A5A0 RenderState_SetRenderState`
    - 直接给出了 PAL4/RenderWare render-state 编号语义
  - `0x769CB0 _rwD3D9RenderStateTextureFilter`
    - 直接给出了 `state 9` 的底层 D3D9 纹理过滤映射
- 当前已经确认的 RenderWare state 语义：
  - `state 1 = raster / texture`
  - `state 6 = z-test`
  - `state 7 = shade mode`
  - `state 9 = texture filter`
  - `state 10 = src blend`
  - `state 11 = dest blend`
  - `state 12 = vertex alpha`
  - `state 20 = cull mode`
- 当前 PAL4 CEGUI 路径的关键默认值：
  - `state 9 = 2`
  - 继续追到 `_rwD3D9RenderStateTextureFilter(...)` 后可确认：
    - `filter 2` 会走线性采样链
    - `filter 1` 更接近 point / nearest
- 对当前问题的意义：
  - 这说明“文字仍然模糊”的瓶颈已经不只是 glyph atlas 分辨率或 quad 对齐
  - PAL4 在最终提交 CEGUI quad 时，确实还会显式把纹理过滤切到线性采样
  - 所以即使 `LoadFontFile` 重同步和 `oversample=2` 都真实生效，最终屏幕观感仍然可能被 sampler 再次抹糊
- 因此当前默认实验方向已经切换为：
  - 在 inject 的 `Hook_CeguiRendererDoRenderWide(...)` 中直接覆盖 CEGUI queued rect 的 `state 9`
  - 2026-04-24 已从固定 point filter 改为面板可选：
    - 勾选 `Nearest / 像素采样` 时写入 `state 9 = 1`
    - 取消勾选时写入 `state 9 = 2`，回到 linear
  - 这样可以继续 A/B 验证“剩余模糊主要来自线性采样”这条假设，而不是把 point filter 当成固定最终方案

## 2026-04-22 Follow-up: GDI glyph rasterization chain and false lead
- 继续往下追“真实 glyph 是谁生成的”时，IDA 里确认了一个更底层的 GDI 链：
  - `0x676769 D3D9_GetGlyphOutline`
    - 直接调用 `GetGlyphOutlineA(...)`
  - `0x679A84 D3D9_GlyphTexture_Create`
    - 单字符 glyph texture 创建
  - `0x679B50 D3D9_CreateGlyphTextures`
    - 按字符范围批量生成 glyph
  - `0x679BE0 D3D9_CreateGlyphTextures_Range`
    - 结合 `GetCharacterPlacementA/W` 生成 glyph range
- 继续向上追调用方时，确认了决定 `LOGFONT.lfHeight` 的初始化函数：
  - `0x676D18`
    - 当前已在 IDA 里更名为 `PalFontBackend_InitializeGdiFont`
    - 这里会把传入描述结构的首字段直接写进 `LOGFONT.lfHeight`
    - 然后调用 `CreateFontIndirectA/W`
- 再往上是两个包装层：
  - `0x672963`
    - 当前已在 IDA 里更名为 `PalFontBackend_Create`
  - `0x6729CD`
    - 当前已在 IDA 里更名为 `PalFontBackend_CreateFromDesc`
    - 这里把上层传来的描述字段组装成 `[height,width,weight,...,face_name]`
- 最上层命中的直接调用点是：
  - `0x419E50`
    - 当前已在 IDA 里更名为 `PalFont_CreateFromDesc`
  - 它的 xref 落到 `0x41A0E0 PalFontManager_InitializeFonts`
  - 而且这里的实参是硬编码的 `Times New Roman`
- 为什么这条线当时没有继续深挖：
  - 因为它已经暴露出明显的“另一套字体系统”特征：
    - 调用点属于 `PalFontManager_InitializeFonts`
    - 字体名是 `Times New Roman`
    - 参数形态是 PAL4 自己的 D3D9/GDI 字体描述结构
  - 这和当前要解决的 `CEGUI dynamic font + 中文对白` 症状不吻合
  - 所以这条线的价值在于：
    - 证明进程里确实存在一套“直接走 GDI 栅格化”的字体后端
    - 帮我们确认 `CreateFontIndirect / GetGlyphOutline` 在 PAL4 中的落点
  - 但它不是当前对白汉字模糊的主链，因此不应在这一轮继续投入
- 当前结论：
  - `PalFontManager_*` 是 PAL4 自己的一套字体/纹理后端，不等于 CEGUI 动态中文字体链
  - 这一轮应该保留这条证据，但把主线继续放回 `CEGUIBase.dll` 导出面的 `Font::unload/load/defineFontGlyphs` 方向

## 2026-04-22 Follow-up: create-time font definition override
- 最新日志已经确认：
  - `notify=2880x2160 oversample=2` 的 resync 确实执行了
  - 但 `font_height_before` 与 `font_height_after` 完全一致
  - 例如 `dialog_simsun` 仍然是 `22 -> 22`
- 这说明单纯在字体对象创建后调用：
  - `setNativeResolution(...)`
  - `notifyScreenResolution(...)`
  并不会让 PAL4 这版 `CEGUIBase.dll` 重建更高分辨率 glyph。
- 同时，`LoadFontFile @ 0x4BD3B0` 的 IDA 证据显示：
  - 它最终只把 `.font` 文件路径传给 `CEGUI::FontManager::createFont(...)`
  - 字号、native resolution、autoscale 等实际创建参数都来自 `.font` XML
- 做过一版更前置的实验路径：
  - 在 `Hook_LoadFontFile(...)` 里为 `system / systemBold / dialog_simsun` 生成 inject 专用 `.font` 定义
  - 新定义在“创建时”同步放大 `Size / NativeHorzRes / NativeVertRes`
- 这版实验已经被证伪并回退：
  - 游戏启动时会在 `CEGUI_LoadResourceData @ 0x416760` 崩溃
  - 崩点固定在 `0x4167DF`
  - 根因是 PAL4 当前状态下的 `DefaultResourceProvider` 优先走 `Package_OpenFile(...)` 包资源路径
  - inject 运行时生成的 `__pal4inject__*.font` 虽然落到了磁盘上，但没有进入 PAL4 的包文件资源索引
  - 于是 `Package_OpenFile(...)` 返回空指针，后续直接解引用 `[edi+0x24]` 触发 `EXCEPTION_ACCESS_VIOLATION`
- 当前结论：
  - “创建时替换成新的 `.font` 文件名”在 PAL4 这条资源加载链上不安全
  - 如果还要继续走 create-time 方向，必须想办法：
    - 保持原始资源名不变
    - 或直接改 `createFont(...)` 后续解析得到的字体定义内容
    - 或更底层地拦截真正的 glyph rasterization 参数

## 2026-04-22 Follow-up: `CEGUIBase.dll` font rebuild chain confirmed
- 当前已经在真实 `CEGUIBase.dll` 里把字体从 `.font` 创建到 glyph atlas 落图的主链追清楚了：
  - `0x100B320F Font::Font(...)`
    - 构造时先把 native resolution 初始化为 `640x480`
    - 然后直接调用 `Font::load(...)`
  - `0x100B64D0 Font::load(...)`
    - 先 `Font::unload(...)`
    - 再通过 XML parser 读取 `.font`
  - `0x100B95E0 FontXmlHandler_elementStart`
    - 这是字体 XML 的核心解析函数
    - 会解析 `<Font>` 上的：
      - `Name`
      - `Filename`
      - `ResourceGroup`
      - `NativeHorzRes`
      - `NativeVertRes`
      - `AutoScaled`
      - `IdeogramFont`
      - `Type`
      - `Size`
      - `AntiAlias`
    - `Type="Dynamic"` 时会调用 `Font::constructor_impl(...)`
    - 继续进入 `Font::createFontFromFT_Face(...)`
    - 同时还会收集：
      - `<GlyphRange StartCodepoint/EndCodepoint>`
      - `<GlyphSet Glyphs="...">`
      - `<Glyph Codepoint=...>`
  - `0x100B6F10 Font::createFontFromFT_Face(...)`
    - 先调用底层 FreeType backend helper 更新 face/size
    - 随后直接调用 `Font::defineFontGlyphs_impl(...)`
  - `0x100B6860 Font::defineFontGlyphs_impl(...)`
    - 计算所需纹理尺寸
    - `undefineAllImages()`
    - 清空 atlas buffer
    - 调用 `Font::createFontGlyphSet(...)`
    - 最后把整张内存 buffer 上传到 glyph texture
  - `0x100B4560 Font::createFontGlyphSet(...)`
    - 逐个 codepoint 做排版和贴图布局
    - 每个 glyph 都会走 `Font::drawGlyphToBuffer(...)`
    - 然后在 `Imageset` 里定义单字符 image
  - `0x100B5190 Font::drawGlyphToBuffer(...)`
    - 把 FreeType 产生的 glyph bitmap 写进 atlas buffer
    - 支持：
      - monochrome bitmap
      - 8-bit grayscale bitmap
- 这条链给出两个关键纠偏：
  - 之前“`notifyScreenResolution` 可能根本没触发 glyph rebuild”这个判断需要修正
  - 在 PAL4 这版 `CEGUIBase.dll` 里，只要字体对象是 FreeType / dynamic font，并且 `AutoScaled=true`，`notifyScreenResolution(...)` 的确会继续进入：
    - `updateFontScaling(...)`
    - `createFontFromFT_Face(...)`
    - `defineFontGlyphs_impl(...)`
- 但这也解释了为什么我们之前的运行时日志会显得矛盾：
  - 日志里 `font_height_before` / `font_height_after` 不变
  - 不代表“没重建”
  - 更可能代表：
    - 这条重建链保留了同一 nominal point size / layout height
    - 真正变化的是底层 FreeType face size、atlas 内容或 glyph bitmap 细节
    - 而不是 CEGUI `getFontHeight()` 这种上层度量值
- 因此当前主线结论要更新为：
  - `notifyScreenResolution(...)` 不是无效
  - sampler 也不是唯一瓶颈
  - 当前更需要确认的是：
    - `createFontFromFT_Face(...)` 里传给底层 FreeType helper 的 size / DPI / scaling 是否真的足以产出更高密度 glyph
    - 以及 PAL4 实际对白字体是否完整走在这条 FreeType dynamic font 链上
- 这也意味着后续 runtime 实验的优先级应该转成：
  1. 绑定并调用 `Font::createFontFromFT_Face(...)` / `Font::defineFontGlyphs(...)` 做更直接的重建实验
  2. 必要时继续下探 `sub_10104510(...)` 这类 FreeType backend helper，确认真正控制 glyph raster size 的输入参数
  3. 不再把“只改 sampler”或“只看 `font_height` 是否变化”当作主要判据

## 2026-04-22 Breakthrough: PAL4 `createFontFromFT_Face(...)` ignores DPI in practice
- 继续下探 `Font::createFontFromFT_Face @ 0x100B6F10` 后，当前已经确认一个非常关键的 PAL4 特化行为：
  - 函数签名虽然是：
    - `createFontFromFT_Face(this, pointSize, horzDpi, vertDpi)`
  - 但它实际调用底层 FreeType size helper 的汇编是：
    - `push pointSize`
    - `push 0`
    - `push face`
    - `call 0x10104510`
- 也就是说：
  - 传进 `createFontFromFT_Face(...)` 的 `horzDpi / vertDpi`
  - 在这版 PAL4 `CEGUIBase.dll` 里并没有继续传到真正的 size 设置 helper
  - 真正参与 FreeType size 设置的只有：
    - `pointSize`
- 同时已经确认：
  - `Font::getPointSize @ 0x100B7C90`
    - 直接返回 `this+0xE0`
  - `Font::constructor_impl @ 0x100B6120`
    - 最终会调用 `createFontFromFT_Face(this, a5, rendererDpiX, rendererDpiY)`
    - 其中 `a5` 就是 `.font` XML 里的 `Size`
- `0x10104510` 当前已在 IDA 里更名为：
  - `FtFace_SetCharSize_Square26p6`
  - 它会把请求值做成：
    - `width = value << 6`
    - `height = value << 6`
  - 然后转给 `FtFace_RequestSize @ 0x10104480`
- 这个结论对当前问题的意义非常直接：
  - 之前我们在 inject 里做的：
    - `setNativeResolution(...)`
    - `notifyScreenResolution(...)`
    - `oversample=2`
  - 即使真的进入了 `updateFontScaling -> createFontFromFT_Face -> defineFontGlyphs_impl`
  - 也不代表底层 FreeType glyph size 一定会变大
  - 因为在 PAL4 这版 DLL 里，真正送到底层 size helper 的仍然只是原始 `pointSize`
- 这就能解释为什么之前会出现这种现象：
  - `notifyScreenResolution(...)` 看起来已经生效
  - 甚至 point filter 也让字体更“硬”
  - 但整体分辨率感并没有真正提高
  - 根因是：
    - glyph atlas 的重建确实发生了
    - 但底层字符栅格尺寸并没有随 `notify/native_res/oversample` 按预期被抬高
- 当前主线结论进一步收敛为：
  - 对 PAL4 这版 `CEGUIBase.dll`，真正控制动态字体 glyph 分辨率的最关键输入是：
    - `.font` 的 `Size`
    - 或 `Font::createFontFromFT_Face(...)` 传入的 `pointSize`
  - 单靠 `notifyScreenResolution/native resolution` 这条链，无法把 glyph 栅格尺寸真正顶上去
- 因此下一步最值得尝试的方向已经非常明确：
  1. 想办法在运行时直接把 `createFontFromFT_Face(...)` 的 `pointSize` 提高
  2. 或者更早地在 `constructor_impl(...)` / XML 解析结果里改写 `Size`
  3. 再配合显示阶段的缩放，把屏幕占用尺寸压回原来的 UI 逻辑大小
  4. 不再把 `notify/native resolution` 作为主要提高清晰度的手段

## 2026-04-22 Runtime experiment: oversample `dialog_simsun` and compensate in `drawText`
- 当前已经把一版最小运行时实验落到 inject：
  - 只针对 `dialog_simsun`
  - 在 `LoadFontFile` 成功并完成原有 resync 后，额外调用：
    - `Font::createFontFromFT_Face(font, pointSize * 2, 0, 0)`
  - 同时在 `CEGUIBase.dll` 的：
    - `Font::drawText(...)`
    上安装一条很窄的 inline hook
  - 对被标记为 oversampled 的字体对象，把：
    - `x_scale`
    - `y_scale`
    都乘以 `0.5`
- 当前实验目标不是“一步到位修完所有 UI 字体”，而是先验证一件最重要的事：
  - 如果只把真实 glyph point size 提高到 `2x`
  - 再在显示阶段把视觉尺寸压回 `0.5x`
  - 是否能得到“字号近似不变，但字形更清晰”的效果
- 这版实现的意义：
  - 它第一次不再依赖 `notifyScreenResolution/native resolution`
  - 而是直接命中已经由 IDA 证实的关键控制点：
    - `pointSize`
  - 同时它也没有去改磁盘上的 `CEGUIBase.dll`
  - 仍然是通过 `runtime.dll` 在运行时改行为
- 当前已知限制：
  - 这版只覆盖 `Font::drawText(...)` 主路径
  - 早期版本还没有同时补 `drawTextLineInStrictWidth / drawTextLineJustified / drawTextLineAverageWidth`
  - 这会导致一种非常典型的副作用：
    - 字体肉眼上已经更实
    - 但字间距被异常拉大
    - 每行视觉上能容纳的字数也跟原版不一致
  - 2026-04-22 继续在 IDA 里确认后，已经能把这个现象精确归因到：
    - `drawText(..., formatting=8)` 会进入 `drawTextLineAverageWidth(...)`
    - 这个分支会先调用 `getFormattedTextExtent(..., 8)` 和 `getTextExtent(...)`
    - 然后把两者差值均分到每个 glyph advance 上
  - 也就是说：
    - 这类问题不只是“文本总宽度算错”
    - 还是“AverageWidth / Justified 这类分支在主动改每个字之间的额外补偿量”
  - 所以后续修复不能只盯着：
    - `getTextExtent(...)`
  - 还必须同时关注：
    - `getFormattedTextExtent(...)`
    - `getWrappedTextExtent(...)`
    - 以及必要时进一步针对：
      - `drawTextLineAverageWidth(...)`
      - `drawTextLineJustified(...)`
- 2026-04-24 起新增一条中风险实验分支：
  - 初版曾尝试把 `system / systemBold` 一起纳入同一套 `2x oversample + draw/metric` 补偿链
  - 从系统说明文本截图可以确认：`system` 长文本里括号/引号等符号宽度异常放大，且控件高度判断也随之漂移，导致行距和换行一起变坏
  - 当前收敛做法不是彻底移除 `system`，而是把它改成更保守的 oversample 比例，并在字体重建后以原始 `fontHeight / lineSpacing / baseline` 为基准做小幅微调
  - 最新截图继续表明：单调 `baseline` 与 `drawText rect.y` 偏移，对 `system` 页签文字位置都几乎没有可见作用
  - 这说明问题层级更可能在 glyph image 的 `offsetY`，而不是字体全局 metrics 或 `drawText` 外围矩形
  - 但直接改 `Imageset::defineImage(...)` 入参里的 `Vector2 offset.y` 会把 `system` 观感拉回高清化之前的显示，因此这条写路径实验已关闭
  - 下一步应改成只观测 `system_auto_glyph_images` 的真实 `offsetY`，不直接改值；`systemBold` 仍保持不额外补偿
  - 这两条系统字体实验都继续刻意不接 `OIRAMLOOK RichText::TextGlyph::getHeight/cacheGlyph`
- 当前运行时日志里会把这一段附加在 `load_font_file` detail 后面：
  - `oversampled_point_size=...`
  - `draw_scale=0.500000`
  - `extent_scale=...`
- 因此下一轮人工验证时，应该优先看：
  1. 对白框正文是否比之前更接近“同字号但更实”
  2. 是否出现行距、换行、裁剪等新副作用
  3. 是否需要继续把缩放补偿扩展到其它 `Font::*draw*` 路径

### TODO
- [ ] 为 `system_auto_glyph_images` 增加只观测、不改值的运行时日志，记录 `defineImage(...)` 传入的 `offsetY`
- [ ] 对比 `Image::setVertScaling()` 后的最终 `Image::getOffsetY()`，确认问题是否出在 glyph image 的缩放后偏移
- [ ] 如果 `offsetY` 本身正常，则继续追 `system` 文本所在控件的消费方布局，而不是再调 font metrics
- [ ] 只有在观测证据充分后，才重新引入更窄的 `offsetY` 修正实验；默认产物保持当前稳定链路

## 2026-04-22 Failed attempt: inline hook at `appendCharacter`
- 这一轮曾尝试把修复点继续下沉到：
  - `Font::appendCharacter(...)`
- 目标本来是：
  - 在 glyph/image 刚生成时，直接把内部 `advance / baseline / lineSpacing / image size / image offsets` 按比例缩回
  - 以此替代外围 `drawText / extent` 补偿
- 但这版尝试已经被证明不能直接沿用当前项目里的“固定 patch span + 简单 trampoline copy”方案：
  - 游戏在字体尚未全部加载完成前就发生 `0xC0000005`
  - 最新崩溃记录：
    - `I:\Games\original\pal4_inject\pal4_inject_crash_pid8832_tid60144_code0xC0000005_tick155234093.txt`
    - 时间：`2026-04-22 15:14:04`
- 当前更可信的原因是：
  - `appendCharacter(...)` 函数开头包含更复杂的 SEH / 栈框架初始化
  - 用当前这种“只按已知前缀抄前几个字节”的 trampoline 方式去截它，风险太高
  - 即使前缀匹配，也不代表被复制的 patch span 足够安全
- 因此这条线当前结论应明确写成：
  - `appendCharacter` 作为“根因修复候选点”仍然值得继续研究
  - 但不能再用当前项目现有的简单 inline hook 方法直接挂它
  - 若后续还要继续碰这里，至少需要：
    1. 更可靠的 x86 指令级 relocation / trampoline 支持
    2. 或换成更早/更晚、但 prologue 更稳定的字体内部修正点
    3. 或直接转向磁盘级 `CEGUIBase.dll` 补丁，而不是 runtime detour
- 这次失败也进一步说明：
  - “修根因”不等于“盲目下沉到更底层函数”
  - 还必须同时满足：
    - patch 点真实影响版式
    - patch 方式对该函数入口是安全的

## 2026-04-22 `launch.exe` 对白分页链新证据
- 当前已在 `launch.exe` 中确认对白分页主链：
  - `giTalk_InvokeScenarioDialogAndVoice @ 0x5DFB10`
  - `dialog_SetText @ 0x4B7E00`
  - `dialog_HandleTextDisplay @ 0x4B8450`
- `dialog_SetText(...)` 只负责把对白文本交给 `CEGUI::Window::setText(...)`，并不直接做分页。
- 真正决定“继续打字 / 换行 / 翻页”的是 `dialog_HandleTextDisplay(...)`，它内部会调用：
  - `CEGUI::OLSDialogEditbox::isTypeEnd(...)`
  - `CEGUI::OLSDialogEditbox::needChangePage(...)`
  - `CEGUI::OLSDialogEditbox::changeLine(...)`
  - `CEGUI::OLSDialogEditbox::changePage(...)`
- 这些 `OLSDialogEditbox` 方法来自 `OIRAMLOOK.dll` 导入，而不是 `launch.exe` 本体实现。

当前高置信度判断：
- `launch.exe` 这条链解释了“为什么画面上还有空白，但文字已经被切到第二页”。
- 但它更像是暴露问题的消费侧，不一定就是根因本体。
- 结合目前实际症状：
  - 字形已经明显更实
  - 但单行容量、行距、翻页时机仍与真实渲染不一致
- 更强的解释仍然是：
  - 当前 oversample 实验已经把外层 `drawText / extent / 部分 vertical metrics` 改到了
  - 但字体对象内部仍有一部分 `glyph advance / glyph image size / line metrics` 没有同步缩回
  - `OIRAMLOOK.dll` 的分页逻辑正在消费这组仍偏大的内部 metric，所以表现成提前换行/翻页

因此截至此轮的优先级结论应明确写成：
1. `launch.exe -> dialog_HandleTextDisplay` 是对白分页异常的关键观测点，应保留独立诊断 hook。
2. 真正的根因修复仍优先考虑字体内部 metric 同步，而不是继续只调 `extent_scale` 这类外围补偿。
3. 若后续切到 `OIRAMLOOK.dll` 深挖，重点应放在确认 `needChangePage(...)` 实际依赖了哪些 font metric，而不是把分页阈值硬改成经验值。

## 2026-04-22 `OIRAMLOOK.dll` 深挖：`needChangePage` 只是状态位
- 当前已经在 `OIRAMLOOK.dll` 中直接确认：
  - `CEGUI::OLSDialogEditbox::needChangePage @ 0x100B8B90`
    - 只有两条指令：
      - `mov al, [ecx+874h]`
      - `retn`
    - 也就是说它并不计算分页条件，只是直接返回 `OLSDialogEditbox + 0x874` 这个字节
  - `CEGUI::OLSDialogEditbox::changeLine @ 0x100B8B80`
    - 只是转发到：
      - `CEGUI::RichText::DialogTextFrame::changeLine(this + 0x754)`
  - `CEGUI::OLSDialogEditbox::changePage @ 0x100B8B70`
    - 只是转发到：
      - `CEGUI::RichText::DialogTextFrame::changePage(this + 0x754)`
  - `CEGUI::OLSDialogEditbox::isTypeEnd @ 0x100B8BA0`
    - 也是转发到：
      - `CEGUI::RichText::DialogTextFrame::isTypeEnd(this + 0x754)`

这意味着：
- `OLSDialogEditbox` 这一层并不是“真正做分页判断的地方”
- 真正维护分页状态的是：
  - `CEGUI::RichText::DialogTextFrame`
- `needChangePage()` 暴露出来的其实只是 `DialogTextFrame` 已经算好的结果位

## 2026-04-22 `DialogTextFrame` 分页判定主链
- 已确认的关键函数：
  - `CEGUI::RichText::DialogTextFrame::changeLine @ 0x1000B510`
  - `CEGUI::RichText::DialogTextFrame::changePage @ 0x1000B5D0`
  - `CEGUI::RichText::DialogTextFrame::getTypedTextHeight @ 0x1000B570`
  - `CEGUI::RichText::DialogTextFrame::isTypeEnd @ 0x1000B610`

### `changeLine(...)`
- 会先推进当前行索引，然后计算：
  - `visible_height = [this+0x114] - [this+0x110]`
  - `typed_text_height = getTypedTextHeight(this, 1.0)`
- 若 `typed_text_height > visible_height`
  - 就把：
    - `[this+0x120] = 1`

### `changePage(...)`
- 会把当前页起始行同步到当前行：
  - `[this+0x100] = [this+0x104]`
- 然后做同样的比较：
  - `typed_text_height`
  - `visible_height`
- 最终把：
  - `[this+0x120] = 1 / 0`

### 与 `OLSDialogEditbox::needChangePage()` 的映射
- `DialogTextFrame` 位于：
  - `OLSDialogEditbox + 0x754`
- 因此：
  - `DialogTextFrame + 0x120`
  - 正好映射到：
    - `OLSDialogEditbox + 0x874`
- 也就是说：
  - `needChangePage()` 返回的就是 `changeLine/changePage` 根据高度比较写进去的结果

当前由 IDA 直接支持的高置信度结论：
- “提前翻页”的直接触发条件不是某个独立分页阈值函数
- 而是：
  - `getTypedTextHeight(this, 1.0) > visible_height`

## 2026-04-22 `getTypedTextHeight(...)` 已确认的语义
- `CEGUI::RichText::DialogTextFrame::getTypedTextHeight @ 0x1000B570`
- 当前已确认其核心逻辑：
  - 从当前页起始行索引 `[this+0x100]` 开始
  - 按行累加一个 line-entry 结构里的 `+4` 浮点值
  - 每累加一行还会额外加上传入参数 `a2`
  - 当前分页调用时传入的是：
    - `a2 = 1.0`
- 伪代码可概括为：
  - `typed_text_height += line[i].height + 1.0`
  - 直到累计到当前正在打字的那一行 `[this+0x104]`

这进一步说明：
- 当前过早翻页并不一定是“可见高度算错”
- 更可能是：
  - 每一行记录下来的 `line[i].height`
  - 或这条链依赖的 font line-height / glyph height
  - 本身已经偏大

## 2026-04-22 与运行时日志的拼合结论
- 运行时已经真实记录到：
  - `before_box=716x103`
  - `pre_need_change_page=1`
  - `post_need_change_page` 会在相邻交互间翻转
- 这与 `DialogTextFrame` 里的高度比较逻辑完全吻合：
  - 对白框逻辑尺寸并不小
  - `Window::getText()` 也始终还是整段文本
  - 真正使其提前翻页的是 `DialogTextFrame` 认为“当前已经打出来的文本高度超过可见高度”

因此到这一轮为止，可以把根因链明确写成：
1. `load_font_file` oversample 实验已经让字形更实。
2. 但字体内部仍有部分 line metric 没有完全同步缩回。
3. `DialogTextFrame::getTypedTextHeight()` 在累加这些仍偏大的行高。
4. `changeLine/changePage()` 用这个偏大的 `typed_text_height` 与可见高度比较。
5. 比较结果写入 `DialogTextFrame + 0x120`，最终由 `OLSDialogEditbox::needChangePage()` 直接暴露成“提前翻页”。

## 2026-04-22 `line[i].height` 的真实来源
- 已继续下探：
  - `CEGUI::RichText::RichTextFrame::formatText_autoBreak @ 0x10005BB0`
  - `CEGUI::RichText::RichTextFrame::reformatLines @ 0x10005FB0`
  - `CEGUI::RichText::DocLine::addGlyph @ 0x10005280`
  - `CEGUI::RichText::TextGlyph::getHeight @ 0x10004B70`

### `DocLine::addGlyph(...)` 的行为
- 每向当前行追加一个 glyph：
  - 行宽 `line.width` 会累加：
    - `glyph->getWidth()`
  - 行高 `line.height` 会更新为：
    - `max(line.height, glyph->getHeight())`
- 也就是说：
  - `getTypedTextHeight(...)` 里按行累加的 `line[i].height`
  - 不是某个独立的“分页专用阈值”
  - 而是每一行在排版阶段记录下来的“本行 glyph 最大高度”

### `TextGlyph::getHeight()` 的行为
- `CEGUI::RichText::TextGlyph::getHeight @ 0x10004B70`
- 当前已确认其实现几乎是直接返回：
  - `*(float *)(font + 208)`
- 这里的 `font`
  - 就是 `TextGlyph` 构造时保存的 `CEGUI::Font*`

### 与 inject 当前实现的字段语义对照
- 当前 inject 实验代码里：
  - `kFontHeightIndex = 51`
  - `kLineSpacingIndex = 52`
  - `kBaselineIndex = 53`
- 也就是按当前代码的字段命名：
  - `font + 204`
    - `fontHeight`
  - `font + 208`
    - `lineSpacing`
  - `font + 212`
    - `baseline`
- 这与 `TextGlyph::getHeight()` 的读取行为拼起来后，可以得到一个更具体的结论：
  - 富文本排版里拿来做“行高”的更像是当前 inject 语义里的 `lineSpacing`
  - 而不只是 `fontHeight`

这点很重要，因为它解释了为什么“字形更实”与“行距/分页仍异常”可以同时存在：
- 外围渲染清晰度更多受 glyph atlas 与 draw-scale 影响
- 但 `DialogTextFrame -> DocLine -> TextGlyph::getHeight()` 这一链更直接依赖 `font + 208`
- 如果这里的语义是 `lineSpacing`
  - 那么剩余异常就要优先围绕 `lineSpacing` 是否真的被稳定缩回去排查

### 对当前 inject 轨的直接含义
- 这说明对白分页链消费的不是外围补偿后的“最终显示尺寸”
- 而是 `CEGUI::Font` 对象内部缓存的某个原始高度字段
- 当前 inject oversample 实验虽然已经补了：
  - `drawText(...)`
  - `getTextExtent(...)`
  - `getFormattedTextExtent(...)`
  - `getWrappedTextExtent(...)`
  - 以及一部分 `vertical metrics`
- 但如果 `TextGlyph::getHeight()` 直接读取的 `font + 208` 没有同步缩回：
  - `DocLine::addGlyph()` 仍会把每一行记得过高
  - `DialogTextFrame::getTypedTextHeight()` 仍会累计出偏大的文本高度
  - 提前翻页、行距偏大、单页容纳字符偏少就仍然会一起出现

因此到这一轮可以把“仍像在到处补外围”的症结进一步收敛为：
1. 当前外围 `drawText / extent` 补偿已经足以让字形看起来更实。
2. 但 `OIRAMLOOK.dll` 的富文本分页并不消费这些外围补偿结果。
3. 它最终消费的是 `TextGlyph::getHeight()` 读到的 `CEGUI::Font` 内部高度字段。
4. 如果这个字段没有和 oversample 后的 draw-scale 同步缩回，就会继续把分页阈值推大。

## 2026-04-22 下一步收敛点
- 继续确认：
  - `CEGUI::Font + 208`
  - 在 `CEGUIBase.dll` 里到底对应哪一个高度字段
- 当前高优先级假设是：
  - 这就是我们还没完全补到的“字体内部缓存高度”
  - 也是当前 inject 轨剩余对白分页异常的最直接根因候选

## 2026-04-22 `CEGUIBase.dll` 字段映射已坐实
- 在 `CEGUIBase.dll` 里已直接确认：
  - `CEGUI::Font::getFontHeight(this, scale)`
    - 返回 `scale * this[51]`
    - 即：
      - `font + 204`
  - `CEGUI::Font::getLineSpacing(this, scale)`
    - 返回 `scale * this[52]`
    - 即：
      - `font + 208`

### 与 `TextGlyph::getHeight()` 的对应
- 之前在 `OIRAMLOOK.dll` 已确认：
  - `CEGUI::RichText::TextGlyph::getHeight()`
    - 直接返回 `*(float *)(font + 208)`
- 现在和 `CEGUIBase.dll` getter 对上之后，可以把它明确翻译成：
  - `TextGlyph::getHeight()` 实际读的是：
    - `Font::lineSpacing`
  - 而不是：
    - `Font::fontHeight`

### `createFontFromFT_Face -> defineFontGlyphs_impl` 对这三个字段的写入
- 已确认：
  - `CEGUI::Font::updateFontScaling()`
    - 对动态字体会直接调用：
      - `createFontFromFT_Face(this, pointSize, horzDpi, vertDpi)`
- `defineFontGlyphs_impl()` 在重建 glyph atlas 后会重写：
  - `this[51]`
    - `fontHeight`
  - `this[52]`
    - `lineSpacing`
  - `this[53]`
    - `baseline`

这意味着：
- 当前 inject 轨如果只验证 `fontHeight`
  - 信息是不够的
- 因为对白分页链真正消费的是：
  - `lineSpacing`
- 因此运行时诊断现在需要同时记录：
  - `font_height`
  - `line_spacing`
- 否则会出现：
  - “字形更实了”
  - 但 `lineSpacing` 仍未缩回
  - 从而继续提前翻页 / 行距偏大 / 单页字数偏少

## 2026-04-22 `OIRAMLOOK.dll` 宽度消费链已确认
- 在继续下探 `RichTextFrame` 的换行逻辑后，已经确认“单行字数变少”不是分页器自己凭空截断，而是走了一条明确的宽度判断链：
  - `CEGUI::RichText::RichTextFrame::formatText_autoBreak @ 0x10005BB0`
  - `CEGUI::RichText::TextGlyph::splitByWidth @ 0x10004B80`
  - `CEGUIBase.dll!CEGUI::Font::getCharAtPixel(...)`

### `formatText_autoBreak(...)` 的行为
- `formatText_autoBreak(...)` 会先尝试把一个 glyph 加到当前 `DocLine`
- 如果：
  - `current_line_width + glyph->getWidth()`
  - 已经超过可用宽度
- 就会改走：
  - `glyph->splitByWidth(remaining_width)`
- 如果 split 成功：
  - 当前行写入切出来的前半段
  - 剩余后半段保留给下一行

### `TextGlyph::splitByWidth(...)` 的行为
- `TextGlyph::splitByWidth(...)`
  - 并不是自己手工逐字符试宽
  - 它直接调用：
    - `CEGUI::Font::getCharAtPixel(font, text, 0, width, 1.0)`
- 也就是说：
  - 真正决定“在当前剩余宽度下最多能放到第几个字”的
  - 不是 `OIRAMLOOK.dll` 自己
  - 而是 `CEGUIBase.dll` 的 `Font::getCharAtPixel(...)`

### 普通文本与稳定宽度文本分别依赖的宽度函数
- 普通 `TextGlyph`
  - `getWidth()`
    - 直接调用：
      - `CEGUIBase.dll!Font::getTextExtent(...)`
- `StableWidthText`
  - `getWidth()`
    - 返回构造时缓存的固定宽度
  - `getTextWidth(...)`
    - 会调用：
      - `CEGUIBase.dll!Font::getFormattedTextExtent(...)`
      - `CEGUIBase.dll!Font::getTextExtent(...)`

### `StableWidthText` 的额外交叉点
- `StableWidthText::getTextWidth(...)` 内部在构造格式化矩形时：
  - 高度参数直接取：
    - `font + 208`
    - 即 `Font::lineSpacing`
- 这说明：
  - 宽度链并不是完全和高度链解耦
  - 但真正决定“切到第几个字”的宽度源头仍然是：
    - `getCharAtPixel(...)`
    - `getTextExtent(...)`
    - `getFormattedTextExtent(...)`

## 2026-04-22 对当前根因判断的修正
- 之前我们已经静态确认：
  - `DialogTextFrame` 的翻页比较确实会看高度链
- 但新的运行时日志显示：
  - `dialog_simsun`
    - `font_height_final = 19.50`
    - `line_spacing_final = 21.72`
  - 当前 `lineSpacing` 并没有继续被 oversample 实验进一步放大
- 因此当前更高置信度的判断应该修正为：
  - 剩余异常不再优先怀疑“高度链被写坏”
  - 而更优先怀疑：
    - 字宽/换行链和 oversample 后的宽度补偿仍未完全一致

换句话说：
1. `OIRAMLOOK.dll` 负责“何时切行、如何拆词”。
2. 但真正决定“一行最多放几个字”的宽度源头已经落在 `CEGUIBase.dll`。
3. 后续如果要继续根因修复，优先应转到 `CEGUIBase.dll` 继续追：
   - `Font::getCharAtPixel(...)`
   - `Font::getTextExtent(...)`
   - `Font::getFormattedTextExtent(...)`

## 2026-04-22 `CEGUIBase.dll` 宽度源头继续收敛
- 当前已确认：
  - `CEGUI::Font::getTextExtent @ 0x100B3A10`
  - `CEGUI::Font::getCharAtPixel @ 0x100B3B70`
  - `CEGUI::Font::getFormattedTextExtent @ 0x100B75A0`
  - `CEGUI::Font::drawTextLineAverageWidth @ 0x100B5E30`
  - `CEGUI::Font::appendCharacter @ 0x100B80A0`

### `getTextExtent(...)` 的真实累加项
- `getTextExtent(...)` 并不是简单查某个“文本总宽缓存”
- 它逐字符查 glyph 记录，然后：
  - 用 `glyph->image width + glyph->image offset` 维护一份 `max_extent`
  - 用 `glyph->advance` 维护一份 `running_pen_x`
- 最终返回两者较大值

这意味着：
- 即使外围 `extent` hook 已经做补偿
- 真正参与内部排版的仍然是 glyph cache 里那几个字段本身

### `getCharAtPixel(...)` 的真实判定依据
- `getCharAtPixel(...)`
  - 不调用 `getTextExtent(...)`
  - 也不做更高层格式化推导
- 它只是逐字符把：
  - `glyph->advance`
  - 按 `scale` 累加到当前 `pen_x`
- 一旦累计宽度超过目标像素位置，就返回当前字符索引

这说明：
- `TextGlyph::splitByWidth(...)` 最终能切到第几个字
- 实际上是由 glyph cache 里的 `advance` 决定的
- 如果 `advance` 仍偏大：
  - 就会更早切行
  - 单行字数变少
  - 总行数变多
  - 最后表现成“明明还有空白却提前翻页”

### `drawTextLineAverageWidth(...)` 只是消费既有宽度
- `drawTextLineAverageWidth(...)`
  - 会先取：
    - `getFormattedTextExtent(..., formatting=8)`
    - `getTextExtent(...)`
  - 再把差值均摊到每个 glyph 间距
- 它不是 glyph 原始宽度来源
- 更像是：
  - 在既有 `advance` 宽度基础上做额外排版分配

### `appendCharacter(...)` 才是 glyph 宽度缓存写入点
- `appendCharacter(...)` 在创建 glyph 记录时会写入：
  - `glyph image`
  - `advance`
  - 原始宽度等字段
- 当前已看到的关键写入是：
  - `*(_DWORD *)(glyph_record + 20) = advance`
- 而：
  - `getCharAtPixel(...)`
  - `getTextExtent(...)`
  - `drawTextLine(...)`
  - `drawTextLineInStrictWidth(...)`
- 都会继续消费这个 `advance`

因此到这一轮，可以把当前最可疑的根因继续收敛为：
1. oversample 实验已经让字形 atlas 更实。
2. 外围 `drawText / extent` 补偿让屏幕观感部分恢复。
3. 但 `appendCharacter(...)` 写入到 glyph cache 里的 `advance` 仍然保留了 oversample 后偏大的语义。
4. `getCharAtPixel(...)` 和内部换行逻辑直接消费这个偏大的 `advance`。
5. 于是会出现：
   - 单行字数偏少
   - 总行数偏多
   - 看起来像“提前翻页”

## 2026-04-22 当前最值得尝试的修复方向
- 不再把主修复方向放在：
  - `fontHeight`
  - `lineSpacing`
  - 单纯外围 `getTextExtent` 补偿
- 当前最值得继续尝试的是：
  - `appendCharacter(...)` 这一层的根因修复
  - 或在 glyph cache 写入后直接把 `advance` 缩回到逻辑字号语义

## 2026-04-22 已落地修复：`getCharAtPixel(...)` 补偿
- 在继续验证后，优先先做了一个比 `appendCharacter` detour 更稳的修复：
  - 对 oversampled 的 `dialog_simsun`
  - 额外 hook：
    - `CEGUI::Font::getCharAtPixel(...)`
- 修复策略不是直接改 glyph cache
- 而是在这个函数里把传入的 `x_scale` 再乘一次：
  - `draw_scale`
- 当前运行时日志已确认这条路径真实命中，例如：
  - `hook=get_char_at_pixel font=dialog_simsun ... scale_in=1 scale_effective=0.5 result=33`

这说明：
- `splitByWidth(...)` 的切分宽度现在终于和实际显示宽度使用了同一套缩放语义
- 因而“每行字数偏少”的主问题已经由这条补偿优先收敛

## 2026-04-22 已落地修复：对话字体 `lineSpacing` 向 `fontHeight` 收敛
- 在宽度问题先被修住后，剩下的主观观感集中到了“行距还偏宽”
- 当前已把 oversampled font 的 vertical metric 再做一层保守收敛：
  - 先按 `draw_scale` 缩放：
    - `fontHeight`
    - `lineSpacing`
    - `baseline`
  - 然后如果：
    - `lineSpacing > fontHeight`
  - 就把：
    - `lineSpacing = fontHeight`

当前最新日志已确认：
- `dialog_simsun`
  - `font_height_final = 19.50`
  - `line_spacing_final = 19.50`

这表示当前运行时已经不再保留更宽的旧 `lineSpacing`
- 后续如果视觉上仍有剩余偏差
  - 优先怀疑的就不再是 `Font::lineSpacing`
  - 而应转向：
    - baseline / image offset
    - 或具体对话窗口自己的绘制布局

## 2026-04-22 已落地实验：把 `baseline` 从 `draw_scale` 中拆成独立参数
- 在 `line_spacing_final=18.27` 已经真实生效后，实际对白观感仍然显示：
  - 每行字数已恢复
  - 但姓名行与正文行之间的视觉垂直间距仍偏大
- 这说明剩余问题不再适合继续通过全局 `lineSpacing` 去压：
  - 当前最值得优先验证的，是 `Font::drawTextLine(...)` 消费到的 glyph 垂直落点
  - 包括：
    - `baseline`
    - `Image::offsetY`
- 因此运行时 oversample 配置现在进一步拆分为：
  - `draw_scale`
  - `extent_scale`
  - `line_spacing_scale`
  - `baseline_scale`
- 当前 `dialog_simsun` 的实验参数为：
  - `draw_scale = 0.5`
  - `extent_scale = 0.5`
  - `line_spacing_scale = 1.0`
  - `baseline_scale = 1.0`
- 2026-04-23 的截图验证显示，额外使用 `0.8 / 0.75` 会把正文首行顶到上边界。
- 因此垂直方向现在只抵消 oversample 本身的 `draw_scale=0.5`，不再额外压缩 line spacing / baseline。
- 当前实现并没有声称已经坐实“根因就是 baseline”
- 这一步的定位更准确是：
  - 在不重新碰宽度链和分页链的前提下
  - 单独验证“把字在每一行里的垂直落点再上提一档”是否能直接命中剩余空隙
- 如果这步有效：
  - 后续就应继续朝 `baseline / image offset` 深挖
- 如果这步无效：
  - 就可以更有把握地把注意力转到 glyph image 的 `offsetY` / `defineImage(...)` 写入链

## 2026-04-23 修正：避免 `drawText` 内部宽度度量被二次横向补偿
- 针对“当前对话文本列间距仍然过大”的反馈，重新检查了当前 oversample 补偿链：
  - `Font::drawText(...)` hook 已经把 `x_scale / y_scale` 按 `draw_scale=0.5` 压回逻辑尺寸
  - 但 `drawText` 原函数内部还会继续调用：
    - `Font::getTextExtent(...)`
    - `Font::getFormattedTextExtent(...)`
    - `Font::getWrappedTextExtent(...)`
    - `Font::getCharAtPixel(...)`
  - 这些度量 hook 旧逻辑还会把返回值或传入 scale 再按 `0.5` 补偿一次
- 这会制造一个非常符合当前症状的错误：
  - `drawTextLineAverageWidth / Justified` 分支看到的文字宽度被算得过窄
  - 于是它为了填满目标 rect，把剩余空间均分到 glyph 间隙
  - 视觉结果就是“每列/每字之间间距仍然过大”
- 当前实现已改成：
  - 在 oversampled `drawText` 调用期间，用线程局部状态标记“这个字体的 scale 已经补偿过”
  - 外部直接调用度量函数时，仍然把 `x_scale` 乘 `extent_scale`
  - `drawText` 内部再调用这些度量函数时，不再做第二次 `extent_scale`
  - `getTextExtent / getFormattedTextExtent / getWrappedTextExtent / getCharAtPixel` 都走同一套横向 scale 判定
- 这次修正的定位：
  - 不继续调大/调小经验间距系数
  - 先修掉 oversample 实验自身造成的横向度量不一致
  - 优先解决 AverageWidth/Justified 分支主动扩字距的问题

## 2026-04-23 修正：对白控件自己的 `LineSpaceExtent`
- 用户继续反馈“没有姓名行也是一样，行与行之间还是过宽”后，重新核对布局资源，确认 `ScenarioDialog/EditDialog` 不是只依赖字体自己的 `lineSpacing`：
  - `I:\Games\PAL4_game\gamedata\decompressedData\ui\layouts\ScenarioDialog.xml`
  - `OiramLook/SDialogEditbox`
  - `Font = dialog_simsun`
  - `LineSpaceExtent = 3.000000`
- 这说明当前剩余行距问题至少还有一层控件级行间距：
  - 字体 runtime 已把 `dialog_simsun` 的 `line_spacing_final` 收敛到约 `17.38`
  - 但 `SDialogEditbox` 仍会额外叠加布局里的 `LineSpaceExtent=3`
  - 所以继续调 `Font::lineSpacing` 会误伤字体全局语义，也不能解释“没有姓名行也一样”
- 当前 runtime 修正：
  - 在 `dialog_HandleTextDisplay` hook 入口拿到 `ScenarioDialog/EditDialog` 的 editbox 指针
  - 首次看到该 editbox 时调用 `Window::setProperty("LineSpaceExtent", "0.000000")`
  - 只作用于剧情对白 hook 路径，不改其它窗口 XML，也不影响装备/任务/帮助等其它大量使用 `LineSpaceExtent` 的界面
  - 成功时事件日志会出现：
    - `hook=dialog_line_space_fix property=LineSpaceExtent value=0.000000`

## 2026-04-23 修正：OIRAMLOOK 富文本行高仍消费未缩放 glyph 高度
- 用户用实际截图继续确认：正文内部两行之间仍然被拉开，已经不是单纯的“姓名行到正文行”间距，也不是 `ScenarioDialog/EditDialog` 的 `LineSpaceExtent` 能解释的量级。
- 重新对照 `OIRAMLOOK.dll` 导出和反汇编后，当前关键点收敛到：
  - `?getHeight@TextGlyph@RichText@CEGUI@@UBEMXZ`
  - RVA `0x00004B70`
  - VA `0x10004B70`
- 该函数实现非常短：
  - `mov eax, [ecx+B4h]`
  - `fld dword ptr [eax+D0h]`
  - `ret`
- 也就是说，OIRAMLOOK 富文本排版不是通过我们已经 hook 的 `Font::drawText(...)` 结果来决定每行高度，而是直接读取 `TextGlyph` 里保存的 `CEGUI::Font*`，再取 `Font + 0xD0`，也就是此前已确认的 `Font::lineSpacing` 字段。
- `LineSpaceExtent=3.000000` 确实存在，也值得归零，但它只会额外贡献少量控件级行距。
- 截图里的正文两行间隔接近“oversample 后 40px 字体的未缩放行高”，所以真正的大头仍在 `TextGlyph::getHeight()` 这条 OIRAMLOOK 富文本链。

Runtime 修复：
- 新增 hook：`OIRAMLOOK.dll!CEGUI::RichText::TextGlyph::getHeight`
- 读取原始 `Font + 0xD0` 高度。
- 如果该高度已经是合理的 17-22px 量级，则原样返回，避免二次缩小。
- 如果它看起来仍是 oversampled `dialog_simsun` 的未缩放行高，则按 `draw_scale * line_spacing_scale` 收回到逻辑对白行高；当前 `line_spacing_scale=1.0`，避免首行顶边被裁。
- 运行时日志会记录前几次命中：
  - `hook=text_glyph_get_height font=dialog_simsun point_size=... raw=... effective=... compensated=...`
- `load_font_file` 的实验摘要也会追加：
  - `rich_text_height_hook=1`

## 2026-04-23 修正：行高收敛后首行人名仍被隐藏
- live log 已确认当前行高补偿命中：
  - `raw=43.4375`
  - `effective=21.7188`
  - `line_spacing_scale=1.0`
  - `baseline_scale=1.0`
- 这说明正文内部行距已经回到合理区间，剩余“首行人名不见”不是继续压行高的问题。
- 当前 CLI / live log 已进一步证明：
  - `ScenarioDialog/EditDialog` 的原始文本仍从 `云天青：` 开头
  - `before_page_start_line=0`
  - `after_page_start_line=0`
  - `after_current_line=4`
  - `post_is_type_end=1`
- 也就是说，首行不是被脚本层或 `page_start_line` 逻辑丢掉，而是绘制时被额外向上挪走了。
- 隐藏滚动条偏移曾是候选解释，但后续 live log 已证伪：
  - `before_scroll=0,0`
  - `after_scroll=0,0`
  - 所以首行不是被旧 `scrollPosition` 推走。
- 继续下探 `OIRAMLOOK.dll!CEGUI::RichText::TextGlyph::cacheGlyph(...)` 后确认更直接的根因：
  - `TextGlyph::getHeight()` 已返回补偿后的逻辑高度 `21.7188`
  - 但 `cacheGlyph(...)` 仍从 `TextGlyph + 0xB4` 取 `Font*`，再用原始 `Font + 0xD0 = 43.4375` 计算 glyph 顶边
  - 其等价关系是 `glyph_top = rect.top - raw_line_spacing - 1`
  - 对首行而言这会得到约 `glyph_top_before=-22.7188`，人名被顶出可视区
- 当前 runtime 修正：
  - 不再尝试移动整个 `EditDialog` 窗口位置
  - 不再保留隐藏滚动条归零试验代码
  - hook `TextGlyph::cacheGlyph(...)`，仅在调用原函数期间把 `Font + 0xD0` 临时替换为补偿后的逻辑高度
  - 原函数返回后立刻恢复原始 `lineSpacing`，避免影响其它 CEGUI 路径
- 成功时事件日志会出现：
  - `hook=text_glyph_cache_glyph raw=43.4375 effective=21.7188 glyph_top_before=-22.7188 glyph_top_after=-1`

## Current Status
- 已完成：
  - 字体资源类型、字号、autoscale/AA 配置确认
  - 原始 EXE `LoadFontFile -> FontManager::createFont` 链确认
  - 上游 CEGUI dynamic font autoscale 机制交叉验证
  - `CEGUIBase.dll` 字体相关导出确认
  - inject 轨已接入 `LoadFontFile` seam，并把 `system / systemBold / dialog_simsun` 的 runtime font resync 做成独立模块
  - inject runtime 现已在 `LoadFontFile` 成功后额外记录已知 dynamic font 的 atlas texture
  - renderer widescreen patch 现已对这些已知字体 atlas 的 quad 单独走“保像素”对齐：
    - 左/上边界向下取整
    - 右/下边界向上取整
  - 这条修复的目标不是放大字体字号，而是减少宽屏非整数缩放把汉字细笔画再吃掉 1 像素
  - 2026-04-22 继续通过 IDA 确认了 `dword_950CD0 + 0x20` 不是“仅绑定 texture”的黑盒，而是 RenderWare 的通用 render-state setter：
    - `state 1 = raster / texture`
    - `state 9 = texture filter`
    - 其中当前 PAL4 CEGUI 路径反复写入 `state 9 = 2`
  - 继续顺着 `_rwD3D9RenderStateTextureFilter(...)` 证实：
    - `filter = 2` 会落到 D3D9 线性采样链
    - `filter = 1` 才是更接近 point / nearest 的路径
  - inject 当前已加入一版验证性修复：
    - 在 `Hook_CeguiRendererDoRenderWide(...)` 里对 CEGUI queued rect 覆盖 `state 9`
    - 2026-04-24 已从固定 point filter 改成 `linear / nearest` 面板可选
    - 这版的目的不是最终 UI 美术定稿，而是保留 A/B 验证“文字主要被 sampler 线性过滤糊掉”的能力
  - 2026-04-23 已修正 oversampled `dialog_simsun` 的横向度量二次补偿问题：
    - `drawText` 内部调用 `getTextExtent / getFormattedTextExtent / getWrappedTextExtent / getCharAtPixel` 时不再重复乘 `0.5`
    - 这应直接收敛 AverageWidth/Justified 分支把字间距摊大的问题
  - 2026-04-23 已确认对白布局本身有 `LineSpaceExtent=3.000000`，并加入剧情对白 editbox 的 runtime 属性覆盖：
    - `LineSpaceExtent -> 0.000000`
    - 这条修复是控件级行间距修正，不再继续全局压字体 `lineSpacing`
  - 2026-04-23 已加入 OIRAMLOOK 富文本行高 hook：
    - `TextGlyph::getHeight` 直接读取 `Font + 0xD0`
    - 若读到的仍是 oversample 未缩放高度，则按 `0.5 * 1.0` 收回
    - 成功时 `load_font_file` 摘要包含 `rich_text_height_hook=1`
  - 2026-04-23 已确认剧情对白首行缺失来自 `TextGlyph::cacheGlyph` 继续使用原始 `Font + 0xD0` 计算 glyph 顶边：
    - 隐藏滚动条偏移已由 live log 证伪
    - runtime 现在会在 `cacheGlyph` 原函数调用期间临时替换 `Font + 0xD0`
    - 成功时 `load_font_file` 摘要包含 `rich_text_cache_glyph_hook=1`
  - 2026-04-22 已在真实 `CEGUIBase.dll` 里确认 FreeType 动态字体重建主链：
    - `Font::load`
    - `FontXmlHandler_elementStart`
    - `Font::createFontFromFT_Face`
    - `Font::defineFontGlyphs_impl`
    - `Font::createFontGlyphSet`
    - `Font::drawGlyphToBuffer`
  - 当前可以高置信度地说：
    - `notifyScreenResolution(...)` 对 dynamic font 并非“完全不重建”
    - 它会经由 `updateFontScaling(...)` 回到 FreeType glyph atlas 生成链
- 当前未完成：
  - 还需要继续确认 `createFontFromFT_Face(...)` 传给底层 FreeType helper 的 size / DPI / scaling 是否真的抬高了 glyph 栅格密度
  - 还需要继续做真实游戏视觉验收，确认对话框正文、主菜单和系统说明页在宽屏下的清晰度改善程度
- 当前建议优先级：
  1. 保留 `LoadFontFile` 字体重同步，因为它已经被运行时日志证实真实生效
  2. 不再把 `font_height_before/after` 不变直接等同于“没重建”
  3. 下一步优先继续下探 `createFontFromFT_Face(...)` 及其底层 FreeType helper，确认真实 glyph 栅格尺寸控制点
  4. point/nearest filter 实验保留为可切换辅证，但不再把它当成主修复方向

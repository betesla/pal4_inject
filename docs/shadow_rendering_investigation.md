# PAL4 Shadow Rendering Investigation

## Scope
这份记录专门沉淀 PAL4 注入轨里对“人物影子在游戏里糊成一坨”的调查链路、当前结论和下一步最值得下手的 runtime patch seam。

## Problem Statement
- 外部查看 `I:\PAL4\resources\decompressedData\PALActor\101\101_shadow.dff` 时，shadow 模型轮廓并不差。
- 但游戏里的实际人物影子明显发糊、发软，更像一团低清投影。
- 当前要回答的核心问题不是“有没有 shadow 模型”，而是：
  - 影子究竟怎么渲染出来
  - 哪一层把它压成了糊影
  - 在 `pal4_inject` 里最值得先 patch 哪几个参数

## Current Conclusion
- PAL4 角色影子不是“直接把 `*_shadow.dff` 画到场景里”。
- 实际链路是：
  1. 加载 `*_shadow.dff`
  2. 绑到主角色同一套骨骼动画
  3. 用专用 shadow camera 把 shadow clump 渲到离屏纹理
  4. 对这张纹理做额外后处理
  5. 再把处理后的 shadow texture 投到 collision world 地面三角形上
- 因此，游戏里影子变成“一坨”的主因更像是：
  - shadow raster 分辨率过低
  - 启用了半分辨率中间纹理
  - 还额外做了多次后处理
  - 最终不是保留原始 shadow mesh，而是作为投影纹理落地

## IDA-Backed Resource And Animation Chain

### 1. Shadow clump load
- `GameResourceManager_LoadShadowClump @ 0x666200`
- 已证实行为：
  - 通过主模型资源路径拼出 `"_shadow.dff"`
  - 调用 `GameWorld_LoadClump(...)`
  - 结果存到 `this[12]`
  - 创建一个额外的 `RpLight` 存到 `this[13]`
  - 为该 light 创建 `RwFrame`
  - 以固定角度旋转后挂到 shadow clump 下

关键结论：
- shadow 不是普通附属 mesh，而是带专用 light/frame 的一套独立资源。

### 2. Shadow clump is bound to the same animation hierarchy
- `GameObject_Initialize @ 0x4428A2`
  - 加载主 clump 后继续调用 `GameResourceManager_LoadShadowClump`
  - 成功后执行：
    - `RpClump_ProcessAtomics_2(this[12])`
    - `Animation_AttachHierarchyToClump(this + 92, this[12])`
- `actorAnimation_play @ 0x52AB65`
  - 更换动作/模型时同样会重新加载 shadow clump
  - 然后再次把同一套 hierarchy 挂到 `this[12]`

关键结论：
- `*_shadow.dff` 不是静态地贴地摆着，而是和主角色一样跟随骨骼动作更新。

## Runtime Shadow Camera Chain

### 1. Shadow camera initialization
- `actorAnimation_setUserData @ 0x52AE46`
  - 末尾会调用 `GameResourceManager_RenderCamera(a1, 1, a3)`
- `GameResourceManager_RenderCamera @ 0x6664A0`
  - 前提：必须同时存在 `this[12]` shadow clump 和 `this[13]` light
  - 调用 `Camera_Init(this + 14, a3, this[12], this[13], this + 52, this + 56)`
  - 初始化成功后立即调用：
    - `Camera_RenderScene((int)(this + 14), 1036831949, 0, (int)this, 0)`

关键结论：
- 游戏专门为 shadow clump 初始化了一套 camera/raster，而不是沿用主相机直接渲染。

### 2. Per-frame update path
- `GameObject_UpdatePosition @ 0x44298B`
- `actorAnimation_pause @ 0x52AC59`
- `PetManager_UpdatePetState @ 0x43E059`
- `Object_HandleEvent_Extended @ 0x59A16C`

这些路径都会调用：
- `GameResourceManager_UpdateAndRenderClump @ 0x667620`

而 `GameResourceManager_UpdateAndRenderClump` 在 shadow camera 已就绪时会继续执行：
- `Camera_RenderScene((int)(v2 + 14), dt, 0, (int)v2, 0)`

关键结论：
- shadow 不是“一次性生成贴图后长期复用”，而是每帧都可能重新更新。

## Offscreen Render And Post-Process Chain

### 1. Render shadow clump into offscreen camera
- `Camera_RenderScene @ 0x5FCAF0`
  - 调用 `Camera_RenderScene_Internal @ 0x5FD880`
- `Camera_RenderScene_Internal @ 0x5FD880`
  - `RwCameraClear(...)`
  - `RwCameraBeginUpdate(...)`
  - `RpClumpRender(a2)`
  - `RwCameraEndUpdate(...)`

这里的 `a2` 就是 shadow clump。

关键结论：
- `101_shadow.dff` 首先被画进离屏 camera，而不是直接成为最终地面影子。

### 2. Shadow texture post-processing
- `Camera_RenderScene @ 0x5FCAF0`
  - 在离屏 render 完成后还会继续执行：
    - `Camera_RenderToTexture @ 0x5FDA60`
    - `Camera_RenderToTexture2 @ 0x5FDC80`
    - `Camera_RenderFog @ 0x5FDE00`（条件开启时）

当前高价值参数：
- `dword_8C27E8 = 6`
  - `Camera_Init @ 0x5FC820` 用 `1 << dword_8C27E8`
  - 当前主 shadow raster 分辨率为 `64x64`
- `dword_8C27F0 = 1`
  - 启用半分辨率中间纹理
- `dword_8C27EC = 2`
  - `Camera_RenderToTexture2` 额外循环 `2` 次
- `flt_8C27F8 = 1.0`
- `flt_8C27F4 = 69.0`

关键结论：
- 当前 shadow pipeline 在资源质量尚可的前提下，仍会先被压到 `64x64`。
- 再叠加半分辨率中间纹理和两次额外处理，非常容易把轮廓抹糊。

## Ground Projection Chain

### 1. Final shadow is projected onto collision world
- `GameResourceManager_RenderClump_2 @ 0x667590`
  - 如果 shadow camera 和 shadow light 都有效，则调用：
    - `Camera_Render(this + 14, a2)`
- `Camera_Render @ 0x5FCA10`
  - 当 `dword_8C27E4 != 0` 且相关条件满足时，调用：
    - `Camera_RenderCollisionWorld(...)`

### 2. Collision-world projection
- `Camera_RenderCollisionWorld @ 0x5FE040`
  - 绑定 shadow texture
  - 设置渲染状态
  - 构造投影用矩阵/缩放/平移
  - 对 collision world 调用：
    - `RpCollisionWorldForAllIntersections(..., RenderCollisionGeometry, ...)`
- `RenderCollisionGeometry @ 0x5FE270`
  - 把落在 receiver 三角形上的 shadow 投影组织成 `RwIm3D` 顶点
  - 最终通过：
    - `RwIm3DTransform`
    - `RwIm3DRenderPrimitive`
    - `RwIm3DEnd`
  - 把投影阴影画到地面几何上

关键结论：
- 游戏里最终看到的影子，本质是“shadow texture 投射到 collision world 上”的结果。
- 所以它的最终观感同时受：
  - shadow texture 分辨率
  - 后处理
  - receiver 三角形分布
  - alpha/blend/state
  的共同影响。

## Related Evidence Notes

### 1. `matrixShadow`
- `matrixShadow @ 0x6C085E`
- 已证实这是标准影子投影矩阵风格的函数：
  - 先取 plane/light 数据
  - 再输出 4x4 projection matrix

当前状态：
- 它已经坐实“引擎存在专门的阴影投影矩阵逻辑”。
- 但在当前 xref 里，还没有直接连到最末级的 collision draw 上。
- 它更像是同一套 shadow/projection 子系统里的辅助数学函数，而不是唯一绘制入口。

### 2. Script-side toggles exist
- 存在字符串：
  - `"void giEnableShadow( bool )" @ 0x8AEDFC`
- 当前只确认到它是 script 注册面的一部分，尚未继续追到最终 runtime flag 改写点。

## Why The In-Game Shadow Becomes A Blob
基于上面的证据，当前最强解释是：

1. `*_shadow.dff` 先被 rasterize 到 `64x64`
2. 还有半分辨率中间纹理
3. 还会走 `2` 次额外处理
4. 最后又不是原始 clump 直接显示，而是投到地面 collision triangles

因此：
- 外部看 shadow 模型质量尚可
- 进游戏后仍然会被这条低分辨率投影链压成模糊阴影

这和“资源模型本身好不好”并不矛盾。

## Best Runtime Patch Seams

### First-priority experiments
最值得先试的 runtime patch 顺序：

1. 提高 shadow texture 分辨率
   - 目标：`dword_8C27E8`
   - 当前：`6` -> `64x64`
   - 建议先试：
     - `8` -> `256x256`
     - `9` -> `512x512`

2. 关闭半分辨率中转
   - 目标：`dword_8C27F0`
   - 当前：`1`
   - 建议先试：`0`

3. 降低额外后处理次数
   - 目标：`dword_8C27EC`
   - 当前：`2`
   - 建议先试：
     - `1`
     - `0`

### Function-level hook candidates
- `Camera_Init @ 0x5FC820`
  - 最适合改 raster 尺寸与相关初始化参数
- `Camera_RenderToTexture2 @ 0x5FDC80`
  - 最适合截断或重写 blur/post-process 次数
- `Camera_RenderCollisionWorld @ 0x5FE040`
  - 最适合观察最终 receiver pass 的纹理绑定、混合与投影强度
- `RenderCollisionGeometry @ 0x5FE270`
  - 最适合继续细抠：
    - alpha 计算
    - 顶点色
    - 投影衰减
    - 地面几何接收质量

## Recommended Injection Strategy
如果在 `pal4_inject` 里做最小有效实验，推荐顺序是：

1. 先做常量 patch，不改控制流
   - 提高 `dword_8C27E8`
   - 关闭 `dword_8C27F0`
   - 降低 `dword_8C27EC`

2. 用运行时日志明确记录当前实际生效值
   - `desired_shadow_resolution_shift`
   - `applied_shadow_resolution_shift`
   - `half_res_enabled`
   - `post_blur_passes`

3. 肉眼验证改善是否已经足够明显
   - 如果已经从“一坨”恢复到“可辨轮廓”
   - 暂时不要过早重写整条 shadow pass

4. 只有在第一轮 patch 收益不足时，再继续下潜到：
   - `RenderCollisionGeometry`
   - `Camera_RenderCollisionWorld`

## Open Questions
- `giEnableShadow(bool)` 最终写到哪个 runtime flag，还需要补齐。
- `matrixShadow @ 0x6C085E` 在当前版本里和 collision projection 的最终接点，还可以继续确认。
- `RenderCollisionGeometry` 里顶点 alpha 的计算是否还能进一步增强轮廓，需要结合 live patch 再观察。
- receiver 端如果地面 collision mesh 本身过粗，也可能进一步放大 shadow 投影的破碎感。

## Current Practical Summary
- PAL4 人物影子的真正问题不是 `*_shadow.dff` 模型差。
- 当前更强的主因是：
  - `64x64` shadow texture
  - 半分辨率中间纹理
  - 两次额外处理
  - 最终再投到 collision world
- 因此，最划算的第一步不是重做 shadow model，而是先 patch shadow-camera 子系统的这几个质量参数。

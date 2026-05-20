# WUILiquidGlassDemo

这个仓库是一个基于 WinUI 2 (system XAML Islands) & C++/WinRT 的 Liquid Glass 风格材质实验项目。它的目标是搭一条完整的实时链路，把“实时背景输入 + 模糊预处理 + 玻璃材质 shader”串起来。

> [!WARNING]
>
> Vibe coded with GPT-5.4 High.

当前实现可以概括为四个部分：

- 实时 backdrop 捕获
- 基于 rAF 风格的按需调度
- 独立的 blur 预处理 pass
- 最终的玻璃材质 pass

## 核心思想

这个项目最核心的设计选择，是把玻璃看成一层“光学透射表面”，并围绕这层表面组织整条渲染链路。

落到实现上，含义是：

1. 实时捕获玻璃后面的内容。
2. 把捕获到的 frame 转成 D3D11 纹理。
3. 在需要时先对这张纹理做独立的高斯模糊预处理。
4. CPU 侧只选择一张最终透射纹理传给 glass shader。
5. 在 glass shader 里根据圆角矩形几何去计算折射、色散、高光、边框和遮罩。

所以当前的链路是偏串行的：

`实时背景 -> 可选 blur 预处理 -> 折射 / 色散采样 -> 高光 / 边框叠加`

## 代码结构

- [BackdropImageGenerator.h](./BackdropImageGenerator.h) / [BackdropImageGenerator.cpp](./BackdropImageGenerator.cpp)
  负责捕获 live backdrop，输出 `Direct3D11CaptureFrame`，并通过 `LastFrame` 暴露最新 frame。
- [AnimationFrameScheduler.h](./AnimationFrameScheduler.h) / [AnimationFrameScheduler.cpp](./AnimationFrameScheduler.cpp)
  提供类似 requestAnimationFrame 的调度能力，用来合并多次重绘请求。
- [MainPage.xaml](./MainPage.xaml)
  承载演示 UI、参数 slider，以及自定义渲染用的 composition 宿主。
- [MainPage.h](./MainPage.h) / [MainPage.cpp](./MainPage.cpp)
  持有 D3D11 资源、blur pass、composition surface，以及最终玻璃材质 shader。
## 全链路说明

### 1. 实时背景捕获

`BackdropImageGenerator` 接收一个 `Visual`，持续产出 `Direct3D11CaptureFrame`。

这个 demo 采用按需触发的重绘模型，触发来源主要有三类：

- backdrop frame 更新
- 渲染宿主尺寸变化
- slider 参数变化

这样做的好处是，渲染时机和 UI 变化一致，不会无意义地持续空转。

> [!WARNING]
>
> 由于 `Windows.Graphics.Capture` 的内在限制，无法正确捕获透明的 Visual 或元素的 handoff visual。必须为目标元素设置实心背景和 Visual 中转。

### 2. rAF 风格调度

`AnimationFrameScheduler` 用来把多次 invalidation 合并成一次待执行的 render request。

它的作用是避免重复注册和重复绘制，让整个链路和 UI rendering cadence 对齐。

### 3. 最终输出载体

`MainPage` 会创建一个 `CompositionDrawingSurface`，然后使用 D3D11 往这张 surface 里画内容。

这张 surface 再通过 `CompositionSurfaceBrush` 挂到 `SpriteVisual` 上显示。这样一来，最终效果仍然保持 composition layer 的集成方式，同时底层材质逻辑保持完全自定义。

### 4. Blur 预处理

当前实现会先执行一个独立的两段 separable Gaussian blur：

- 横向 blur pass
- 纵向 blur pass

blur 完成后得到一张预处理过的透射纹理。

当 blur 强度等于 0 时，则直接使用原始 frame 作为透射输入。

### 5. 最终玻璃材质 pass

最终 shader 只接收一张透射纹理：

- blur 关闭时，是原始 frame
- blur 开启时，是预处理后的 blurred frame

然后在这个 pass 里完成下面这些工作：

- rounded-rect 遮罩
- 场函数计算
- 折射偏移
- 色散采样
- 内部提白
- 边框增强
- 内 glow 与高光叠加

这一步才是真正让结果更像玻璃的关键。

## 算子顺序

当前实现的算子顺序是明确固定的：

1. 获取最新 live frame
2. 在需要时执行 Gaussian blur 预处理
3. 选择最终 transmission texture
4. 在采样阶段施加 refraction 和 dispersion
5. 叠加高光、glow、边框等表面响应
6. 最后应用 rounded-rect alpha mask

可以简写成：

`F -> optional Blur(F) -> sample with refraction and dispersion -> add surface lighting terms -> mask`

## 几何场与材质场

rounded rect 在这里不只是一个 clip mask。

它同时驱动：

- 折射衰减
- 色散衰减
- 边框强调
- 高光强度

这里有一个实现上的关键点：真正的 rounded-rect SDF 内部深度并不适合直接拿来驱动所有材质项。因为在 medial axis 附近，它天然会形成偏轴对齐的 plateau，如果把它当成大范围中心衰减场，就容易出现可见断层。

所以当前 shader 有意把两类场分开：

- 基于真实 SDF 的边缘距离场，用于 edge-local 的项
- 单独构造的 rounded interior field，用于 refraction / dispersion 的中心衰减

这样分离的目的，是为了同时满足两件事：

- 保住边缘项的原始语义
- 避免中心断层和参数回归

## 当前实现方向

当前代码大体遵循下面这些原则：

- 用 live backdrop 作为输入
- blur 作为预处理子系统存在
- 最终 glass pass 只吃一张 transmission texture
- 折射和高光由几何驱动
- 色散只作为受控的边缘光学项存在

它当前仍然是一个 demo，重点在于把关键链路搭清楚；但现在的架构已经允许继续沿着这条光学链路细化，而不需要推倒整个工程重写。

## 运行说明

- 项目使用 WinUI composition + 自定义 D3D11 render path。
- overlay 画进 `CompositionDrawingSurface`，再通过 composition visual 显示。
- slider 会实时驱动材质参数更新。

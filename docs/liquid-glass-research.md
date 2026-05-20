# Apple Liquid Glass 资料整理与复现原理

## 范围

这份文档只做两件事：

1. 整理 Apple 官方资料里能够较稳妥确认的信息。
2. 结合第三方实现，总结复现这类效果时应当遵循的原理。

这份文档暂时不展开具体的 `D3D11` 落地设计。

## 官方资料

从 Apple 的 WWDC 演讲、技术概览和框架文档来看，`Liquid Glass` 被定义为一种新的系统材质，而不是传统意义上的“背景模糊”。

官方反复强调的特征包括：

- 它是动态的，不是静态贴图或固定滤镜。
- 它会根据背后的内容和当前交互状态自适应变化。
- 它支持多个玻璃元素之间的融合、分离和形变。
- 它同时包含背景侧和前景侧的光学处理。

主要官方参考资料：

- WWDC25: Get to know the new design system  
  `https://developer.apple.com/videos/play/wwdc2025/219/`
- WWDC25: Build an app with Liquid Glass  
  `https://developer.apple.com/videos/play/wwdc2025/356/`
- Technology overview: Liquid Glass  
  `https://developer.apple.com/documentation/technologyoverviews/liquid-glass`
- Technology overview: Adopting Liquid Glass  
  `https://developer.apple.com/documentation/technologyoverviews/adopting-liquid-glass`
- SwiftUI: Applying Liquid Glass to custom views  
  `https://developer.apple.com/documentation/swiftui/applying-liquid-glass-to-custom-views`
- SwiftUI: `glassEffect(_:in:)`  
  `https://developer.apple.com/documentation/swiftui/view/glasseffect%28_%3Ain%3A%29`
- SwiftUI: `GlassEffectContainer`  
  `https://developer.apple.com/documentation/swiftui/glasseffectcontainer`
- UIKit: `UIGlassEffect`  
  `https://developer.apple.com/documentation/uikit/uiglasseffect`
- UIKit: `UIGlassContainerEffect`  
  `https://developer.apple.com/documentation/UIKit/UIGlassContainerEffect`
- AppKit: `NSGlassEffectView`  
  `https://developer.apple.com/documentation/appkit/nsglasseffectview`
- AppKit: `NSGlassEffectContainerView`  
  `https://developer.apple.com/documentation/appkit/nsglasseffectcontainerview`

## 可以较高置信度确认的结论

### 1. Liquid Glass 不是普通 blur

官方描述里反复出现 `lensing`、`refraction`、`highlights`、动态适配等表述。这和“高斯模糊 + 半透明白底”不是一回事。

### 2. 它至少是前后两层视觉处理

SwiftUI 的 `glassEffect(_:in:)` 文档明确提到：

- 在 view 后面锚定一个 glass shape
- 在 view 前面叠加 Liquid Glass 的前景效果

这说明它不是单层 quad 上的一次简单合成。

### 3. 形状是核心输入，不是后处理遮罩

Apple 提供的是 shape、container、effect 这类抽象，而不是单纯暴露“模糊半径”或“透明度”之类参数。这意味着玻璃区域的几何形状本身就是材质计算的重要输入。

### 4. 多元素分组渲染是设计的一部分

`GlassEffectContainer`、`UIGlassContainerEffect`、`NSGlassEffectContainerView` 这些 API 的存在，说明 Apple 明确希望多个玻璃元素以“组”的方式参与渲染，而不是彼此完全独立地画出来。

### 5. 交互态会影响材质本身

官方不仅描述了形状变化，也描述了滚动、强调、过渡时的响应。这说明系统不仅关心最终像素，也关心状态变化和几何变化过程。

## 第三方实现带来的补充信息

有参考价值的第三方资料包括：

- Lickability `LiquidGlassKit`  
  `https://github.com/Lickability/LiquidGlassKit`
- OverShifted `LiquidGlass`  
  `https://github.com/OverShifted/LiquidGlass`
- Imad Rahman 的实现笔记  
  `https://imadr.me/liquid-glass/`
- Ken Sorrell 的效果拆解  
  `https://www.sorrell.info/blog/liquid-glass-lens-effect`

这些实现的平台和完整度各不相同，但会持续收敛到几类共同做法：

- 效果由 shape field 驱动，而不只是一个矩形蒙版。
- 真正区分度最高的是折射或采样坐标偏移，而不是 blur 本身。
- 边缘高光、反射感、描边感通常是独立的一层。
- 如果要做多个玻璃块互相融合，就必须先做某种 union / smooth merge。
- blur 阶段通常会单独实现，而不是顺手在主材质 shader 里做几次邻域采样。

第三方实现不能直接证明 Apple 内部代码一定就是这样写的，但它们能很好地说明：如果缺少这些部件，视觉上通常很难接近 Apple 的效果。

### 第三方实现中的 blur 方案

第三方实现里，blur 阶段并不完全统一，但大方向比较明确：

- 一类实现直接在主 shader 中使用显式高斯权重做采样。
- 一类实现把 blur 单独拆成 pass，用 separable blur 或 Gaussian-like blur。
- 更像实时图形项目的实现，往往还会配合 downscale、intermediate render target 和多次 blur 迭代。

例如：

- `liquid-glass-js` 的 shader 直接使用 `sigma` 和 `exp(-(distance * distance) / (2 * sigma * sigma))` 这样的高斯式权重，并在较大的采样核上累积结果。
- `OverShifted/LiquidGlass` 则提供了独立的 `Blur.glsl`，并且有单独的 blur pass、降采样比例和多次迭代参数。

这意味着如果要做一个更像样的复现版本，blur 最好被视为单独的子系统，而不是主材质 shader 里的一段临时近似。

### 第三方实现中的色散

第三方实现里，色散并不是每个项目都会做，但它经常以两种方式出现：

- 显式的 RGB 通道偏移
- 非显式的边缘彩色高光、带色边缘反射或微弱的通道不对齐

很多项目即使没有把它单独命名为 chromatic dispersion，也会在边缘附近加入轻微的色彩分离，从而增强“厚玻璃”和“光学表面”的感觉。

不过第三方实现也说明了一点：

没有色散，效果仍然可以看起来像玻璃；
但如果完全没有色散线索，成品通常会更偏“高级毛玻璃”，而不是“真实液态玻璃”。

## 修正后的整体理解

综合官方资料和第三方实现后，更稳妥的说法是：

`Liquid Glass` 本质上是一套围绕形状、分组、实时背景输入和光学形变构建的系统材质。

这比“它是一个更高级的模糊材质”要准确得多。

## 复现原理

如果目标不是“做一个有玻璃氛围的卡片”，而是尽量复现 Liquid Glass 的视觉逻辑，那么至少应当遵循下面这些原则。

### 1. 把背景当作实时光学输入

材质必须读取并响应玻璃后方的真实内容。如果背景输入是静态的，或者只是粗糙近似，那么成品很快就会失去“玻璃”应有的真实感。

### 2. 把玻璃几何当作数据，而不是最后一步的 mask

圆角、邻近玻璃块的融合、边缘厚度感、局部放大/压缩感，都依赖几何参与计算。也就是说，玻璃区域的形状不能只是最后拿来裁剪像素。

### 3. Blur 是必要条件，但远远不够

背景模糊确实能提供基础的前后景分离，但它只能带来传统毛玻璃的观感。Liquid Glass 需要更多光学结构。

### 4. 折射是最关键的区分项

如果没有依据局部几何去改变采样坐标，效果通常就只会停留在“模糊卡片”。而 Liquid Glass 之所以像“液态玻璃”，核心就在于那种局部弯折、放大、压缩、流动的采样特征。

### 5. 高光应当从几何和状态中导出

边缘亮线、反射感、镜面高光，不应只是手工叠一张固定贴图。它们应当随着形状、边界方向、运动状态和背景明暗共同变化。

### 6. 多个玻璃元素在视觉上相关时，应当一起求值

如果每个元素都独立渲染，最终更像几张彼此分离的半透明卡片，而不是同一片液态材质。要做出 Apple 那种连成一体再分开的效果，必须有组级别的表示和合成。

### 7. 交互不应只改 transform

按压、悬浮、过渡、滚动等状态，不能只靠缩放、平移、透明度变化来表达。更接近原始效果的做法，是让这些状态直接影响玻璃形状场或光学参数。

### 8. Scroll-edge 效果应单独看待

Apple 的 scroll-edge blur 更像一种专门用于分离滚动内容与悬浮玻璃控件的自适应模糊逻辑。它不应和所有 glass control 的基础材质逻辑混为一谈。

### 9. 色散不是第一优先级，但应当在高仿版本中纳入考虑

严格来说，玻璃的光学表现不只有模糊、折射和高光，还包括一定程度的色散。

在 UI 场景里，这种色散通常不应被做得很强，因为：

- 强色散很容易看起来像廉价滤镜
- 文字和图标附近容易出现脏边
- 动态内容下会明显影响可读性

但如果目标是高仿而不是基础版，那么完全不考虑色散也是不够的。

更合理的做法通常是：

- 只在边缘内外一小圈引入很弱的 RGB 偏移
- 让色散强度跟边缘法线、厚度感或入射角近似相关
- 让中心区域基本不出现明显通道分离

换句话说，色散更适合被当成“边缘光学微调”，而不是主效果来源。

## 理论实现模型

如果只从理论结构出发，不绑定具体图形 API，那么一个比较完整的 Liquid Glass 实现通常应当包含下面几层。

### 1. 场景输入层

这一层需要准备三类输入：

- 玻璃后方的实时背景图像
- 玻璃元素的几何描述
- 当前交互状态和环境参数

其中：

- 背景图像用于提供被折射、被模糊、被重新采样的源数据。
- 几何描述用于定义玻璃区域本身，以及多个区域之间如何融合。
- 状态参数用于驱动强调、按压、滚动、过渡、亮度适配等动态行为。

如果缺少这三类输入中的任何一类，效果都会退化。

### 2. 几何表示层

理论上最合适的表示不是“先画一个圆角矩形，再拿 alpha 去裁剪”，而是建立一套可连续求值的 shape field。

常见做法是把每个玻璃元素表示为：

- 一个基础形状
- 一组连续可调的参数

例如：

- 位置
- 尺寸
- 圆角半径
- 边缘厚度
- 与其他元素的融合半径
- 当前交互态的形变量

然后在屏幕空间上对这些形状求值，得到一张连续场。这个场至少应当能回答几件事：

- 当前像素是否在玻璃区域内
- 当前像素离边界有多远
- 边界朝向是什么
- 附近是否存在应当融合的邻近玻璃区域

如果想做出“靠近时连成一片、拉开时再分离”的行为，这一步通常要支持 union 或 smooth union，而不是简单地把多个 mask 相加。

### 3. 背景预处理层

背景图像不应被直接原样使用，一般需要先构建适合后续光学采样的版本。

理论上至少需要两类背景数据：

- 原始或较清晰的背景
- 一张低频模糊后的背景

原因很直接：

- 清晰背景适合承担折射后仍保留结构变化的部分。
- 模糊背景适合承担玻璃材质的分离感和柔化感。

如果只保留模糊背景，效果容易变成普通毛玻璃。
如果只保留清晰背景，效果又会显得像透明塑料而不是玻璃。

### 4. 光学形变层

这是最关键的一层。

有了 shape field 之后，需要从中进一步导出一个“光学扰动场”。这个扰动场本质上描述的是：

- 当前像素应该向哪个方向偏移采样
- 偏移量有多大
- 靠近边缘时是否需要更强的透镜感
- 在厚度较大的区域是否需要更平缓的采样变化

换句话说，玻璃不是简单地“盖在背景上”，而是要对背景采样坐标做连续变换。

理论上这一层的输出通常包括：

- 一组用于折射的 UV 偏移
- 一组用于计算高光和描边的局部法线或边界方向
- 一组可选的、用于边缘色散的通道级微小偏移

从视觉上看，是否有“液态感”，主要就取决于这一层是否成立。

### 5. 材质合成层

在有了背景数据和光学扰动场后，下一步才是把玻璃材质本身合成出来。

理论上应当至少拆成三个子部分：

- 玻璃内部主体
- 玻璃边缘层
- 玻璃前景高光层

玻璃内部主体通常负责：

- 在清晰背景和模糊背景之间做混合
- 根据折射场重新采样背景
- 根据环境亮度做局部的明暗补偿

玻璃边缘层通常负责：

- 边缘亮度提升
- 厚度感
- 轮廓可读性

玻璃前景高光层通常负责：

- 镜面高光
- 反射感
- 随交互态变化的能量感

如果追求更高保真度，还可以在这里加入很弱的边缘色散层：

- 对 R、G、B 通道施加略有差别的采样偏移
- 偏移只在边缘或高曲率区域可见
- 强度必须受到严格限制

如果把这三者全部揉成一层，往往很难同时兼顾主体的稳定性和边缘/高光的表现力。

### 6. 分组融合层

多个玻璃元素如果属于同一视觉组，理论上不应先各自独立渲染再简单叠加。

更合理的顺序通常是：

1. 先收集这一组内的所有 glass shape
2. 在组空间内求它们的连续联合形状
3. 基于这个联合结果生成统一的 shape field 和光学扰动场
4. 再渲染这一整片玻璃材质
5. 最后把各自的前景内容放回到这片材质之上

这样做的好处是：

- 邻近元素能自然地连接和分裂
- 边缘高光不会在连接处出现错误的双边线
- 折射场能在组内部保持连续

这也是为什么 Apple 要专门提供 container 级 API。

### 7. 前后景分层合成

理论上最终合成时，至少应当区分三层：

1. 原始背景内容
2. 玻璃材质层
3. 玻璃上的真实前景内容和前景光学装饰层

其中第二层和第三层不能混为一谈。

因为：

- 玻璃主体应当像材质一样响应背后的内容
- 前景内容应当保持可读性和交互性
- 前景高光则应当附着在玻璃表面，而不是附着在文字或图标本身

这也解释了为什么官方文档会强调“后面一个 glass shape，前面一层 foreground effects”。

### 8. 动态更新机制

理论上这个系统不应只在控件自身属性变化时重绘，而应当在以下变化发生时重算：

- 背景内容变化
- 玻璃几何变化
- 元素之间的相对位置变化
- 交互状态变化
- 环境亮度或主题变化

也就是说，它本质上是一个持续响应场景变化的材质系统，而不是静态贴图。

## 一条更接近实现的抽象流水线

如果压缩成一条抽象流水线，大致可以写成：

1. 收集 glass group 内所有元素的几何参数。
2. 生成组级别的连续 shape field。
3. 从 shape field 导出边界距离、法线方向、融合区域和厚度相关参数。
4. 准备背景输入，包括清晰背景和模糊背景。
5. 根据 shape field 生成折射或采样坐标偏移场。
6. 用偏移场对背景做重新采样，生成玻璃主体。
7. 再根据边界信息生成边缘亮化、描边和高光层。
8. 如有需要，再叠加一层很弱的边缘色散修饰。
9. 把玻璃主体、边缘层、前景高光层和可选色散层合成为一张玻璃表面。
10. 最后把真实控件内容绘制到玻璃材质之上。

这条流水线里最容易被误删、但又最关键的两个环节是：

- 第 2 步的连续 shape field
- 第 5 步的折射偏移场

没有这两步，就很难从“毛玻璃卡片”走到“液态玻璃”。

## 复现优先级建议

如果是工程落地而不是纯理论分析，比较合理的优先级通常是：

1. 实时背景输入
2. 独立 blur pass
3. 连续 shape field
4. 稳定的折射偏移场
5. 边缘高光和厚度感
6. 分组与融合
7. 很弱的边缘色散

这个顺序很重要。

色散确实值得做，但通常不应排在 blur、折射和高光之前。
如果前面几项没站稳，先加色散往往只会让结果更花，而不会更像。

## 不应误认为“已经复现”的几类简化版本

下面这些做法可以做出演示效果，但不应与完整的 Liquid Glass 混为一谈：

- backdrop blur 加白色半透明填充
- 一个模糊矩形再叠一圈亮边
- 用固定反射贴图覆盖在控件表面
- 每个控件独立渲染，不做分组与融合

这些做法可以接近气质，但很难接近行为。

## 一句话心智模型

如果必须用一句话概括：

`Liquid Glass` 更像一层由实时背景输入和形状驱动的系统级光学表面，而不是传统意义上的半透明面板。

## 参考链接

- Apple WWDC25 设计场次：`https://developer.apple.com/videos/play/wwdc2025/219/`
- Apple WWDC25 实现介绍：`https://developer.apple.com/videos/play/wwdc2025/356/`
- Apple Liquid Glass 总览：`https://developer.apple.com/documentation/technologyoverviews/liquid-glass`
- Apple adopting Liquid Glass：`https://developer.apple.com/documentation/technologyoverviews/adopting-liquid-glass`
- SwiftUI custom views：`https://developer.apple.com/documentation/swiftui/applying-liquid-glass-to-custom-views`
- SwiftUI `glassEffect(_:in:)`：`https://developer.apple.com/documentation/swiftui/view/glasseffect%28_%3Ain%3A%29`
- SwiftUI `GlassEffectContainer`：`https://developer.apple.com/documentation/swiftui/glasseffectcontainer`
- UIKit `UIGlassEffect`：`https://developer.apple.com/documentation/uikit/uiglasseffect`
- UIKit `UIGlassContainerEffect`：`https://developer.apple.com/documentation/UIKit/UIGlassContainerEffect`
- AppKit `NSGlassEffectView`：`https://developer.apple.com/documentation/appkit/nsglasseffectview`
- AppKit `NSGlassEffectContainerView`：`https://developer.apple.com/documentation/appkit/nsglasseffectcontainerview`
- Lickability `LiquidGlassKit`：`https://github.com/Lickability/LiquidGlassKit`
- OverShifted `LiquidGlass`：`https://github.com/OverShifted/LiquidGlass`
- Imad Rahman 笔记：`https://imadr.me/liquid-glass/`
- Ken Sorrell 拆解：`https://www.sorrell.info/blog/liquid-glass-lens-effect`

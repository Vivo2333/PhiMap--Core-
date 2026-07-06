# PhiMap Core - 维度空间映射引擎

<p align="center">
  <b>纯 Python 实现的维度空间映射算法</b><br>
  <code>Φ = V ∘ K ∘ P ∘ ∂ ∘ D</code>
</p>

---

## 📖 简介

PhiMap Core 是一个基于**维度空间映射**理论的算法引擎，通过五层算子链将连续像素空间映射到特征空间，提取曲率极值点作为关键特征。

核心思想：将图像/信号数据通过微分→边界→参数化→曲率→特征点的流水线处理，实现高效的结构分析。

---

## 🔧 算子链说明

| 算子 | 名称 | 功能 |
|:----:|:-----|:-----|
| **D** | 离散化算子 (Discretization) | 连续像素块 → 栅格统计（均值、方差、梯度） |
| **∂** | 边界提取算子 (Boundary) | 梯度阈值提取显著边界点 |
| **P** | 参数化算子 (Parameterization) | 边界点 → 有序参数曲线 |
| **K** | 曲率算子 (Curvature) | 计算局部曲率 κ(s) |
| **V** | 特征点提取算子 (Vertex) | 曲率极值点检测 |

**复合映射：**
```
Φ = V ∘ K ∘ P ∘ ∂ ∘ D
```

---

## ✨ 特性

- ✅ **纯 Python 实现** — 零第三方依赖，仅使用标准库
- ✅ **分层网格分析** — 支持 8×8 粗网格 + 4×4 细网格（按需激活）
- ✅ **高效轻量** — 适合嵌入式和移动端部署
- ✅ **模块化设计** — 每个算子可独立调用或替换

---

## 🚀 快速开始

### 安装

无需安装，直接下载 `phi_engine.py` 即可使用。

```bash
git clone https://github.com/你的用户名/PhiMap-Core.git
```

### 基础用法

```python
from phi_engine import PhiEngine

# 创建一个 4×4 的测试数据块
patch = [
    [10, 20, 30, 40],
    [50, 60, 70, 80],
    [90, 100, 110, 120],
    [130, 140, 150, 160]
]

# 执行完整算子链
engine = PhiEngine(grid_size=4)
result = engine.execute(patch)

print(f"均值: {result['stats']['mean']:.2f}")
print(f"方差: {result['stats']['variance']:.2f}")
print(f"峰值数: {result['peak_count']}")
print(f"最大曲率: {result['curvature_max']:.4f}")
```

**输出：**
```
均值: 85.00
方差: 2125.00
峰值数: 0
最大曲率: 0.0000
```

---

## 🏗️ 分层网格分析

对于大规模数据，使用分层网格管理器进行粗粒度全局分析：

```python
from hierarchical_grid import HierarchicalGrid
import random

# 生成 64×64 的测试帧
frame = [random.randint(0, 255) for _ in range(64 * 64)]

# 创建分层网格（8×8 粗网格）
grid = HierarchicalGrid(frame_width=64, frame_height=64)

# L1 层分析
results = grid.analyze_l1(frame)

for r in results[:3]:
    print(f"位置 {r['pos']}: 均值={r['mean']:.2f}, 方差={r['variance']:.2f}")
```

**输出：**
```
位置 (0, 0): 均值=122.02, 方差=4224.30
位置 (0, 1): 均值=141.56, 方差=5002.84
位置 (0, 2): 均值=119.91, 方差=6664.62
```

---

## 📐 算法原理

### 1. 离散化算子 D

将连续像素块转换为栅格统计量：

```
mean = ΣΣ patch[i][j] / n²
variance = ΣΣ (patch[i][j] - mean)² / n²
grad_x[i][j] = patch[i][j+1] - patch[i][j-1]
grad_y[i][j] = patch[i+1][j] - patch[i-1][j]
magnitude = √(grad_x² + grad_y²)
```

### 2. 边界提取算子 ∂

基于梯度幅值阈值提取显著边界：

```
threshold = mean(magnitude) × 0.3
boundary = {(i,j) | magnitude[i][j] > threshold}
```

取前 50% 强梯度点作为候选边界。

### 3. 参数化算子 P

将边界点按极角排序，形成闭合参数曲线：

```
cx = mean(x_i), cy = mean(y_i)
θ_i = atan2(y_i - cy, x_i - cx)
gamma = sort(boundary, by θ_i)
```

### 4. 曲率算子 K

计算参数曲线的局部曲率：

```
κ = |dx₁·dy₂ - dy₁·dx₂| / (|v₁|·|v₂|·(|v₁|+|v₂|)/2)
```

其中 v₁, v₂ 为相邻弦向量。

### 5. 特征点提取算子 V

检测曲率局部极大值点：

```
peaks = {i | κ[i] > κ[i-1] and κ[i] > κ[i+1] and κ[i] > 0.05}
```

---

## 📁 项目结构

```
PhiMap-Core/
├── phi_engine.py          # 核心引擎（PhiEngine 类）
├── hierarchical_grid.py     # 分层网格管理器
├── example.py               # 使用示例
├── README.md                # 项目说明
└── LICENSE                  # MIT 开源协议
```

---

## 🔬 应用场景

- **图像特征提取** — 边缘/角点检测
- **信号峰值检测** — 一维信号极值分析
- **网格数据分析** — 传感器数据分块处理
- **嵌入式视觉** — 轻量级特征引擎

---

## 📜 许可证

MIT License — 可自由使用、修改、分发，保留版权声明即可。

---

## 🤝 贡献

欢迎提交 Issue 和 PR！

---

<p align="center">
  <i>PhiMap — 从维度中看见结构</i>
</p>

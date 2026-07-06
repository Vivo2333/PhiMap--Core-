"""
PhiMap Core - 维度空间映射引擎
分层网格管理器
"""

import math
import random


# ========== PhiEngine 核心引擎 ==========

class PhiEngine:
    """通用维度空间映射引擎"""

    def __init__(self, grid_size=8):
        self.grid_size = grid_size

    def D(self, patch):
        """离散化算子"""
        n = len(patch)
        if n == 0 or len(patch[0]) == 0:
            return {
                'mean': 0, 'variance': 0,
                'grad_x': [[]], 'grad_y': [[]], 'magnitude': [[]]
            }

        mean = sum(sum(row) for row in patch) / (n * n)
        variance = sum(
            (patch[i][j] - mean) ** 2
            for i in range(n) for j in range(n)
        ) / (n * n)

        grad_x = []
        grad_y = []
        for i in range(n):
            row_x = []
            row_y = []
            for j in range(n):
                gx = patch[i][min(j+1, n-1)] - patch[i][max(j-1, 0)]
                gy = patch[min(i+1, n-1)][j] - patch[max(i-1, 0)][j]
                row_x.append(gx)
                row_y.append(gy)
            grad_x.append(row_x)
            grad_y.append(row_y)

        magnitude = [
            [math.sqrt(grad_x[i][j]**2 + grad_y[i][j]**2) for j in range(n)]
            for i in range(n)
        ]

        return {
            'mean': mean,
            'variance': variance,
            'grad_x': grad_x,
            'grad_y': grad_y,
            'magnitude': magnitude
        }

    def partial(self, stats):
        """边界提取算子"""
        mag = stats['magnitude']
        n = len(mag)
        if n == 0 or not mag[0]:
            return []

        threshold = sum(sum(row) for row in mag) / (n * n) * 0.3
        boundary = []
        for i in range(n):
            for j in range(n):
                if mag[i][j] > threshold:
                    boundary.append((i, j, mag[i][j]))

        boundary.sort(key=lambda x: x[2], reverse=True)
        boundary = boundary[:max(4, len(boundary)//2)]
        return boundary

    def P(self, boundary):
        """参数化算子"""
        if len(boundary) < 3:
            return boundary

        cx = sum(p[0] for p in boundary) / len(boundary)
        cy = sum(p[1] for p in boundary) / len(boundary)

        sorted_pts = sorted(
            boundary,
            key=lambda p: math.atan2(p[0]-cx, p[1]-cy)
        )
        return sorted_pts

    def K(self, gamma):
        """曲率算子"""
        if len(gamma) < 3:
            return []

        kappa = []
        m = len(gamma)

        for i in range(m):
            p_prev = gamma[(i-1) % m]
            p_curr = gamma[i]
            p_next = gamma[(i+1) % m]

            x1, y1 = p_prev[0], p_prev[1]
            x2, y2 = p_curr[0], p_curr[1]
            x3, y3 = p_next[0], p_next[1]

            dx1, dy1 = x2 - x1, y2 - y1
            dx2, dy2 = x3 - x2, y3 - y2

            cross = abs(dx1 * dy2 - dy1 * dx2)
            len1 = math.sqrt(dx1**2 + dy1**2) + 1e-6
            len2 = math.sqrt(dx2**2 + dy2**2) + 1e-6

            k = cross / (len1 * len2 * (len1 + len2) * 0.5 + 1e-6)
            kappa.append(k)

        return kappa

    def V(self, kappa, gamma):
        """特征点提取算子"""
        if len(kappa) < 3:
            return []

        peaks = []
        for i in range(len(kappa)):
            prev_i = (i - 1) % len(kappa)
            next_i = (i + 1) % len(kappa)

            if kappa[i] > kappa[prev_i] and kappa[i] > kappa[next_i]:
                if kappa[i] > 0.05:
                    peaks.append({
                        'index': i,
                        'pos': (gamma[i][0], gamma[i][1]),
                        'kappa': kappa[i]
                    })

        return peaks

    def execute(self, patch):
        """执行完整算子链"""
        stats = self.D(patch)
        boundary = self.partial(stats)
        gamma = self.P(boundary)
        kappa = self.K(gamma)
        peaks = self.V(kappa, gamma)

        return {
            'stats': stats,
            'boundary': boundary,
            'gamma': gamma,
            'kappa': kappa,
            'peaks': peaks,
            'curvature_mean': sum(kappa)/len(kappa) if kappa else 0,
            'curvature_max': max(kappa) if kappa else 0,
            'peak_count': len(peaks)
        }


# ========== HierarchicalGrid 分层网格 ==========

class HierarchicalGrid:
    """8x8 粗网格 -> 4x4 细网格"""

    def __init__(self, frame_width=64, frame_height=64):
        self.w = frame_width
        self.h = frame_height
        self.phi_8 = PhiEngine(8)
        self.phi_4 = PhiEngine(4)

    def extract_patches(self, frame, grid_size):
        """提取不重叠网格块"""
        patches = []
        step = self.w // grid_size

        for i in range(grid_size):
            for j in range(grid_size):
                patch = []
                for y in range(i * step, (i + 1) * step):
                    row = []
                    for x in range(j * step, (j + 1) * step):
                        idx = y * self.w + x
                        row.append(frame[idx] if idx < len(frame) else 0)
                    patch.append(row)

                patches.append({
                    'grid_i': i,
                    'grid_j': j,
                    'patch': patch,
                    'global_y': i * step,
                    'global_x': j * step
                })

        return patches

    def analyze_l1(self, frame):
        """L1: 8x8 粗网格全局分析"""
        patches = self.extract_patches(frame, 8)
        results = []

        for p in patches:
            phi = self.phi_8.execute(p['patch'])
            results.append({
                'pos': (p['grid_i'], p['grid_j']),
                'global': (p['global_y'], p['global_x']),
                'phi': phi,
                'mean': phi['stats']['mean'],
                'variance': phi['stats']['variance']
            })

        return results


# ========== 测试代码 ==========

if __name__ == "__main__":
    print("=" * 50)
    print("PhiMap Core 测试")
    print("=" * 50)

    # 测试 1: 单个 PhiEngine
    print("\n[测试 1] PhiEngine 4x4")
    engine = PhiEngine(4)
    patch = [
        [10, 20, 30, 40],
        [50, 60, 70, 80],
        [90, 100, 110, 120],
        [130, 140, 150, 160]
    ]
    result = engine.execute(patch)
    print(f"  峰值数: {result['peak_count']}")
    print(f"  均值: {result['stats']['mean']:.2f}")
    print(f"  方差: {result['stats']['variance']:.2f}")

    # 测试 2: HierarchicalGrid
    print("\n[测试 2] HierarchicalGrid 8x8")
    frame = [random.randint(0, 255) for _ in range(64 * 64)]
    grid = HierarchicalGrid()
    results = grid.analyze_l1(frame)

    print(f"  共 {len(results)} 个网格块")
    for r in results[:3]:
        print(f"  位置 {r['pos']}: 均值={r['mean']:.2f}, 方差={r['variance']:.2f}, 峰值={r['phi']['peak_count']}")

    print("\n" + "=" * 50)
    print("测试完成！")
    print("=" * 50)

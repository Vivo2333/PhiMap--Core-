// ================================================================
// phimap_color_calibration.cpp
// PhiMap 颜色校准模块 —— 精简生产版
// 功能：
//   1. 读取 24bit BMP 图像
//   2. sRGB Gamma 逆变换 -> 线性 RGB
//   3. 白平衡（手动锚点 / 灰度世界）
//   4. 3x3 颜色校正矩阵 Q（RGB 点状三色盘）
//   5. 保存校准后的 BMP
// 编译：g++ -O3 -std=c++11 -o color_calib phimap_color_calibration.cpp
// 运行：./color_calib input.bmp output.bmp
// ================================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <string>

// ========== 基础数据结构 ==========
struct RGBPixel {
    uint8_t r, g, b;
};

struct LinearRGB {
    float r, g, b;
};

// ========== BMP 读写（纯标准库） ==========
#pragma pack(push, 1)
struct BMPFileHeader {
    uint16_t type = 0x4D42; // 'BM'
    uint32_t size;
    uint16_t reserved1 = 0;
    uint16_t reserved2 = 0;
    uint32_t offset = 54;
};
struct BMPInfoHeader {
    uint32_t size = 40;
    int32_t width;
    int32_t height;
    uint16_t planes = 1;
    uint16_t bits = 24;
    uint32_t compression = 0;
    uint32_t imageSize;
    int32_t xPPM = 2835;
    int32_t yPPM = 2835;
    uint32_t colorsUsed = 0;
    uint32_t colorsImportant = 0;
};
#pragma pack(pop)

struct BMPImage {
    int width, height;
    std::vector<RGBPixel> pixels;
    bool valid = false;
    std::string error;
};

BMPImage loadBMP(const std::string& filename) {
    BMPImage img;
    std::ifstream file(filename, std::ios::binary);
    if (!file) { img.error = "Cannot open file: " + filename; return img; }

    BMPFileHeader fh;
    BMPInfoHeader ih;
    file.read(reinterpret_cast<char*>(&fh), sizeof(fh));
    file.read(reinterpret_cast<char*>(&ih), sizeof(ih));

    if (fh.type != 0x4D42 || ih.bits != 24) {
        img.error = "Only 24-bit BMP supported"; return img;
    }

    img.width = ih.width;
    img.height = ih.height;
    img.pixels.resize(img.width * img.height);

    int rowSize = ((img.width * 3 + 3) / 4) * 4;
    std::vector<uint8_t> row(rowSize);

    for (int y = img.height - 1; y >= 0; --y) {
        file.read(reinterpret_cast<char*>(row.data()), rowSize);
        for (int x = 0; x < img.width; ++x) {
            int idx = x * 3;
            img.pixels[y * img.width + x] = {row[idx+2], row[idx+1], row[idx]};
        }
    }
    img.valid = true;
    return img;
}

void saveBMP(const std::string& filename, const BMPImage& img) {
    int rowSize = ((img.width * 3 + 3) / 4) * 4;
    BMPFileHeader fh;
    BMPInfoHeader ih;
    ih.width = img.width;
    ih.height = img.height;
    ih.imageSize = rowSize * img.height;
    fh.size = sizeof(fh) + sizeof(ih) + ih.imageSize;

    std::ofstream file(filename, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&fh), sizeof(fh));
    file.write(reinterpret_cast<const char*>(&ih), sizeof(ih));

    std::vector<uint8_t> row(rowSize, 0);
    for (int y = img.height - 1; y >= 0; --y) {
        std::fill(row.begin(), row.end(), 0);
        for (int x = 0; x < img.width; ++x) {
            const auto& p = img.pixels[y * img.width + x];
            row[x*3] = p.b;
            row[x*3+1] = p.g;
            row[x*3+2] = p.r;
        }
        file.write(reinterpret_cast<const char*>(row.data()), rowSize);
    }
}

// ========== 颜色校准引擎 ==========
class ColorCalibration {
public:
    float lut[256][3];
    float wb_gain[3] = {1.0f, 1.0f, 1.0f};
    float Q[3][3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    };
    bool useMatrix = false;

    static float linearize(uint8_t v) {
        float x = v / 255.0f;
        return (x <= 0.04045f) ? (x / 12.92f) : std::pow((x + 0.055f) / 1.055f, 2.4f);
    }

    void buildLUT() {
        for (int v = 0; v < 256; ++v) {
            float lin = linearize(v);
            lut[v][0] = lin * wb_gain[0];
            lut[v][1] = lin * wb_gain[1];
            lut[v][2] = lin * wb_gain[2];
        }
    }

    void estimateWB_GrayWorld(const BMPImage& img) {
        double sumR = 0, sumG = 0, sumB = 0;
        for (const auto& p : img.pixels) {
            sumR += linearize(p.r);
            sumG += linearize(p.g);
            sumB += linearize(p.b);
        }
        int n = img.width * img.height;
        float meanR = sumR / n;
        float meanG = sumG / n;
        float meanB = sumB / n;
        wb_gain[0] = meanG / (meanR + 1e-6f);
        wb_gain[1] = 1.0f;
        wb_gain[2] = meanG / (meanB + 1e-6f);
        buildLUT();
        useMatrix = false;
        std::cout << "[WB] Gray World: gainR=" << wb_gain[0] 
                  << " gainB=" << wb_gain[2] << std::endl;
    }

    void estimateWB_Manual(const BMPImage& img, int x1, int y1, int x2, int y2) {
        x1 = std::max(0, x1); y1 = std::max(0, y1);
        x2 = std::min(img.width, x2); y2 = std::min(img.height, y2);
        double sumR = 0, sumG = 0, sumB = 0;
        int count = 0;
        for (int y = y1; y < y2; ++y) {
            for (int x = x1; x < x2; ++x) {
                const auto& p = img.pixels[y * img.width + x];
                sumR += linearize(p.r);
                sumG += linearize(p.g);
                sumB += linearize(p.b);
                ++count;
            }
        }
        if (count == 0) { estimateWB_GrayWorld(img); return; }
        float refR = sumR / count;
        float refG = sumG / count;
        float refB = sumB / count;
        wb_gain[0] = refG / (refR + 1e-6f);
        wb_gain[1] = 1.0f;
        wb_gain[2] = refG / (refB + 1e-6f);
        buildLUT();
        useMatrix = false;
        std::cout << "[WB] Manual Anchor: gainR=" << wb_gain[0] 
                  << " gainB=" << wb_gain[2] << " (samples=" << count << ")" << std::endl;
    }

    void estimateColorMatrix(
        const BMPImage& img,
        int wx1, int wy1, int wx2, int wy2, const LinearRGB& whiteTarget,
        int a1x1, int a1y1, int a1x2, int a1y2, const LinearRGB& aux1Target,
        int a2x1, int a2y1, int a2x2, int a2y2, const LinearRGB& aux2Target
    ) {
        estimateWB_Manual(img, wx1, wy1, wx2, wy2);

        auto sample = [&](int x1, int y1, int x2, int y2) -> LinearRGB {
            x1 = std::max(0, x1); y1 = std::max(0, y1);
            x2 = std::min(img.width, x2); y2 = std::min(img.height, y2);
            double sr=0, sg=0, sb=0; int c=0;
            for (int y=y1; y<y2; ++y) for (int x=x1; x<x2; ++x) {
                const auto& p = img.pixels[y*img.width+x];
                sr += lut[p.r][0]; sg += lut[p.g][1]; sb += lut[p.b][2]; ++c;
            }
            return {float(sr/c), float(sg/c), float(sb/c)};
        };

        LinearRGB s1 = sample(wx1, wy1, wx2, wy2);
        LinearRGB s2 = sample(a1x1, a1y1, a1x2, a1y2);
        LinearRGB s3 = sample(a2x1, a2y1, a2x2, a2y2);

        float T[3][3] = {
            {whiteTarget.r, aux1Target.r, aux2Target.r},
            {whiteTarget.g, aux1Target.g, aux2Target.g},
            {whiteTarget.b, aux1Target.b, aux2Target.b}
        };
        float S[3][3] = {
            {s1.r, s2.r, s3.r},
            {s1.g, s2.g, s3.g},
            {s1.b, s2.b, s3.b}
        };

        float invS[3][3];
        if (invert3x3(S, invS)) {
            multiply3x3(T, invS, Q);
            useMatrix = true;
            std::cout << "[ColorMatrix] Q computed successfully." << std::endl;
        } else {
            std::cout << "[ColorMatrix] Singular matrix, fallback to WB only." << std::endl;
            useMatrix = false;
        }
    }

    LinearRGB processPixel(uint8_t r8, uint8_t g8, uint8_t b8) const {
        LinearRGB lin = {lut[r8][0], lut[g8][1], lut[b8][2]};
        if (!useMatrix) return lin;
        LinearRGB out;
        out.r = Q[0][0]*lin.r + Q[0][1]*lin.g + Q[0][2]*lin.b;
        out.g = Q[1][0]*lin.r + Q[1][1]*lin.g + Q[1][2]*lin.b;
        out.b = Q[2][0]*lin.r + Q[2][1]*lin.g + Q[2][2]*lin.b;
        return out;
    }

    static uint8_t delinearize(float v) {
        if (v <= 0.0f) return 0;
        float x = (v <= 0.0031308f) ? (v * 12.92f) : (1.055f * std::pow(v, 1.0f/2.4f) - 0.055f);
        if (x >= 1.0f) return 255;
        return static_cast<uint8_t>(x * 255.0f + 0.5f);
    }

private:
    static float det3(const float m[3][3]) {
        return m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])
             - m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])
             + m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    }

    static bool invert3x3(const float in[3][3], float out[3][3]) {
        float inv[3][3];
        inv[0][0] =  (in[1][1]*in[2][2]-in[1][2]*in[2][1]);
        inv[0][1] = -(in[0][1]*in[2][2]-in[0][2]*in[2][1]);
        inv[0][2] =  (in[0][1]*in[1][2]-in[0][2]*in[1][1]);
        inv[1][0] = -(in[1][0]*in[2][2]-in[1][2]*in[2][0]);
        inv[1][1] =  (in[0][0]*in[2][2]-in[0][2]*in[2][0]);
        inv[1][2] = -(in[0][0]*in[1][2]-in[0][2]*in[1][0]);
        inv[2][0] =  (in[1][0]*in[2][1]-in[1][1]*in[2][0]);
        inv[2][1] = -(in[0][0]*in[2][1]-in[0][1]*in[2][0]);
        inv[2][2] =  (in[0][0]*in[1][1]-in[0][1]*in[1][0]);
        float d = in[0][0]*inv[0][0] + in[0][1]*inv[1][0] + in[0][2]*inv[2][0];
        if (std::fabs(d) < 1e-6f) return false;
        float id = 1.0f / d;
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) out[j][i] = inv[i][j] * id;
        return true;
    }

    static void multiply3x3(const float a[3][3], const float b[3][3], float out[3][3]) {
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
            out[i][j] = a[i][0]*b[0][j] + a[i][1]*b[1][j] + a[i][2]*b[2][j];
        }
    }
};

// ========== 主程序 ==========
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <input.bmp> <output.bmp> [mode]" << std::endl;
        std::cout << "  mode: 0=GrayWorld  1=ManualWB  2=ColorMatrix3x3" << std::endl;
        std::cout << "Example: " << argv[0] << " shelf.bmp out.bmp 1" << std::endl;
        return 1;
    }

    std::string inFile = argv[1];
    std::string outFile = argv[2];
    int mode = (argc >= 4) ? std::atoi(argv[3]) : 1;

    auto img = loadBMP(inFile);
    if (!img.valid) {
        std::cerr << "Load failed: " << img.error << std::endl;
        return 1;
    }
    std::cout << "Loaded: " << img.width << "x" << img.height << std::endl;

    ColorCalibration calib;

    if (mode == 0) {
        calib.estimateWB_GrayWorld(img);
    } else if (mode == 1) {
        calib.estimateWB_Manual(img, 0, img.height*2/3, img.width/3, img.height);
    } else {
        calib.estimateColorMatrix(
            img,
            0, img.height*2/3, img.width/3, img.height,          {1.0f, 1.0f, 1.0f},
            img.width/3, img.height/3, img.width*2/3, img.height*2/3, {0.8f, 0.2f, 0.2f},
            img.width*2/3, 0, img.width, img.height/3,              {0.2f, 0.6f, 0.3f}
        );
    }

    BMPImage out = img;
    for (auto& p : out.pixels) {
        auto lin = calib.processPixel(p.r, p.g, p.b);
        p.r = ColorCalibration::delinearize(lin.r);
        p.g = ColorCalibration::delinearize(lin.g);
        p.b = ColorCalibration::delinearize(lin.b);
    }

    saveBMP(outFile, out);
    std::cout << "Saved: " << outFile << std::endl;
    return 0;
}

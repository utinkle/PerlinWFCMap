#define _USE_MATH_DEFINES
#include "NoiseGenerator.h"
#include "ParallelUtils.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <functional>
#include <set>

namespace MapGenerator {
namespace internal {

// 优化的柏林噪声实现
class PerlinNoiseImp {
private:
    static const int permutation[256];
    int p[512];
    
public:
    PerlinNoiseImp(uint32_t seed = 12345) {
        std::mt19937 rng(seed);

        // 初始化排列表
        for (int i = 0; i < 256; i++) {
            p[i] = i;
        }

        // 洗牌
        for (int i = 255; i > 0; i--) {
            int j = rng() % (i + 1);
            std::swap(p[i], p[j]);
        }

        // 复制到后半部分
        for (int i = 0; i < 256; i++) {
            p[256 + i] = p[i];
        }
    }
    
    float noise(float x, float y, float z = 0) const {
        int X = (int)floor(x) & 255;
        int Y = (int)floor(y) & 255;
        int Z = (int)floor(z) & 255;

        x -= floor(x);
        y -= floor(y);
        z -= floor(z);

        float u = fade(x);
        float v = fade(y);
        float w = fade(z);

        int A = p[X] + Y;
        int AA = p[A] + Z;
        int AB = p[A + 1] + Z;
        int B = p[X + 1] + Y;
        int BA = p[B] + Z;
        int BB = p[B + 1] + Z;

        // 修正：使用正确的梯度计算
        return lerp(w, lerp(v, lerp(u, grad3(p[AA], x, y, z),
                                       grad3(p[BA], x - 1, y, z)),
                               lerp(u, grad3(p[AB], x, y - 1, z),
                                       grad3(p[BB], x - 1, y - 1, z))),
                       lerp(v, lerp(u, grad3(p[AA + 1], x, y, z - 1),
                                       grad3(p[BA + 1], x - 1, y, z - 1)),
                               lerp(u, grad3(p[AB + 1], x, y - 1, z - 1),
                                       grad3(p[BB + 1], x - 1, y - 1, z - 1))));
    }
    
private:
    static float fade(float t) {
        return t * t * t * (t * (t * 6 - 15) + 10);
    }
    
    static float lerp(float t, float a, float b) {
        return a + t * (b - a);
    }
    
    // 使用原始的Perlin噪声梯度计算
    static float grad3(int hash, float x, float y, float z) {
        int h = hash & 15;                      // 将低4位作为梯度选择
        float u = h < 8 ? x : y;                 // 根据第0位选择x或y
        float v = h < 4 ? y : (h == 12 || h == 14 ? x : z); // 根据第1-2位选择
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v); // 根据低2位决定符号
    }
};

// Simplex噪声实现
class SimplexNoiseImpl {
private:
    static const int grad3[12][3];
    static const int grad4[32][4];
    static const int simplex[64][4];
    int perm[512];
    
public:
    SimplexNoiseImpl(uint32_t seed) {
        std::mt19937 rng(seed);
        
        for (int i = 0; i < 256; i++) {
            perm[i] = i;
        }
        
        // 洗牌
        for (int i = 255; i > 0; i--) {
            int j = rng() % (i + 1);
            std::swap(perm[i], perm[j]);
        }
        
        for (int i = 0; i < 256; i++) {
            perm[256 + i] = perm[i];
        }
    }
    
    float noise(float x, float y) const {
        const float F2 = 0.366025403f; // (sqrt(3)-1)/2
        const float G2 = 0.211324865f; // (3-sqrt(3))/6
        
        float s = (x + y) * F2;
        int i = fastFloor(x + s);
        int j = fastFloor(y + s);
        
        float t = (i + j) * G2;
        float X0 = i - t;
        float Y0 = j - t;
        float x0 = x - X0;
        float y0 = y - Y0;
        
        int i1, j1;
        if (x0 > y0) {
            i1 = 1; j1 = 0;
        } else {
            i1 = 0; j1 = 1;
        }
        
        float x1 = x0 - i1 + G2;
        float y1 = y0 - j1 + G2;
        float x2 = x0 - 1.0f + 2.0f * G2;
        float y2 = y0 - 1.0f + 2.0f * G2;
        
        int ii = i & 255;
        int jj = j & 255;
        int gi0 = perm[ii + perm[jj]] % 12;
        int gi1 = perm[ii + i1 + perm[jj + j1]] % 12;
        int gi2 = perm[ii + 1 + perm[jj + 1]] % 12;
        
        float t0 = 0.5f - x0 * x0 - y0 * y0;
        float n0 = 0.0f;
        if (t0 > 0) {
            t0 *= t0;
            n0 = t0 * t0 * dot(grad3[gi0], x0, y0);
        }
        
        float t1 = 0.5f - x1 * x1 - y1 * y1;
        float n1 = 0.0f;
        if (t1 > 0) {
            t1 *= t1;
            n1 = t1 * t1 * dot(grad3[gi1], x1, y1);
        }
        
        float t2 = 0.5f - x2 * x2 - y2 * y2;
        float n2 = 0.0f;
        if (t2 > 0) {
            t2 *= t2;
            n2 = t2 * t2 * dot(grad3[gi2], x2, y2);
        }
        
        return 70.0f * (n0 + n1 + n2);
    }
    
private:
    static int fastFloor(float x) {
        return x > 0 ? (int)x : (int)x - 1;
    }
    
    static float dot(const int* g, float x, float y) {
        return g[0] * x + g[1] * y;
    }
};

// Worley噪声（细胞噪声）
class WorleyNoise {
private:
    struct Point {
        float x, y;
        Point(float x = 0, float y = 0) : x(x), y(y) {}
    };
    
    std::vector<std::vector<Point>> grid;
    int gridSize;
    
public:
    WorleyNoise(uint32_t seed, int gridSize = 10) : gridSize(gridSize) {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        
        grid.resize(gridSize);
        for (auto& row : grid) {
            row.resize(gridSize);
            for (auto& point : row) {
                point.x = dist(rng);
                point.y = dist(rng);
            }
        }
    }
    
    float noise(float x, float y, int feature = 0) const {
        // 归一化到[0,1]
        x = fmod(fabs(x), 1.0f);
        y = fmod(fabs(y), 1.0f);
        
        int cellX = (int)(x * gridSize) % gridSize;
        int cellY = (int)(y * gridSize) % gridSize;
        
        std::vector<float> distances;
        
        // 检查当前单元格和相邻单元格
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = (cellX + dx + gridSize) % gridSize;
                int ny = (cellY + dy + gridSize) % gridSize;
                
                float pointX = grid[ny][nx].x + dx;
                float pointY = grid[ny][nx].y + dy;
                
                float distance = sqrt(pow(x * gridSize - (cellX + pointX), 2) + 
                                      pow(y * gridSize - (cellY + pointY), 2));
                distances.push_back(distance);
            }
        }
        
        std::sort(distances.begin(), distances.end());
        
        if (feature < distances.size()) {
            return 1.0f - distances[feature] / (sqrt(2.0f) * gridSize);
        }
        return 0.0f;
    }
};

// NoiseGenerator::Impl 实现
class NoiseGenerator::Impl {
private:
    std::mt19937 m_rng;
    uint32_t m_seed;
    PerlinNoiseImp m_perlin;
    SimplexNoiseImpl m_simplex;
    std::unique_ptr<ParallelProcessor> m_parallelProcessor;

public:
    Impl(uint32_t seed) 
        : m_seed(seed), m_rng(seed), m_perlin(seed), m_simplex(seed) 
        , m_parallelProcessor(std::make_unique<ParallelProcessor>(std::thread::hardware_concurrency()))
    {
    }
    
    HeightMap generateNoise(uint32_t width, uint32_t height, const NoiseParams& params) {
        HeightMap result(width * height);
        generateNoiseParallel(result, width, height, params);
        // 并行后处理
        applyNoisePostProcessingParallel(result, width, height, params);
        
        return result;
    }

    void generateNoiseParallel(HeightMap& result, uint32_t width, uint32_t height,
                              const NoiseParams& params) {
        
        m_parallelProcessor->parallelFor2D(width, height, [&](uint32_t x, uint32_t y) {
            float nx = x / params.scale;
            float ny = y / params.scale;
            
            float value = 0.0f;
            float amplitude = 1.0f;
            float frequency = 1.0f;
            float maxValue = 0.0f;
            
            for (int i = 0; i < params.octaves; i++) {
                float noiseValue = 0.0f;
                
                switch (params.type) {
                    case NoiseType::PERLIN:
                        noiseValue = m_perlin.noise(nx * frequency, ny * frequency);
                        break;
                    case NoiseType::SIMPLEX:
                        noiseValue = (m_simplex.noise(nx * frequency, ny * frequency) + 1.0f) * 0.5f;
                        break;
                    // ... 其他噪声类型 ...
                    default:
                        noiseValue = m_perlin.noise(nx * frequency, ny * frequency);
                        break;
                }
                
                value += noiseValue * amplitude;
                maxValue += amplitude;
                amplitude *= params.persistence;
                frequency *= params.lacunarity;
            }
            
            if (maxValue > 0) {
                value /= maxValue;
            }
            
            result[y * width + x] = value;
        });
    }

    void applyNoisePostProcessingParallel(HeightMap& noise, uint32_t width, uint32_t height,
                                         const NoiseParams& params) {
        
        // 并行应用岛模式
        if (params.islandMode) {
            m_parallelProcessor->parallelFor2D(width, height, [&](uint32_t x, uint32_t y) {
                float dx = (x / static_cast<float>(width)) - 0.5f;
                float dy = (y / static_cast<float>(height)) - 0.5f;
                float distance = sqrt(dx * dx + dy * dy) * 2.0f;
                
                float falloff = 1.0f - distance;
                falloff = std::max(0.0f, falloff);
                
                noise[y * width + x] *= falloff;
            });
        }
        
        // 并行应用域扭曲
        if (params.domainWarp.enabled) {
            applyDomainWarpParallel(noise, width, height, params.domainWarp);
        }
    }
    
    void applyDomainWarpParallel(HeightMap& heightmap, uint32_t width, uint32_t height,
                                const NoiseParams::DomainWarp& warp) {
        
        HeightMap warped(width * height);
        
        m_parallelProcessor->parallelFor2D(width, height, [&](uint32_t x, uint32_t y) {
            float nx = x / warp.frequency;
            float ny = y / warp.frequency;
            
            // 计算扭曲偏移
            float dx = m_perlin.noise(nx, ny, 0.5f) * 2.0f - 1.0f;
            float dy = m_perlin.noise(nx + 5.2f, ny + 1.3f, 0.5f) * 2.0f - 1.0f;
            
            // 应用倍频扭曲
            if (warp.octaves > 1) {
                float amplitude = 0.5f;
                float frequency = 2.0f;
                
                for (uint32_t i = 1; i < warp.octaves; i++) {
                    dx += m_perlin.noise(nx * frequency, ny * frequency, 0.5f + i) * 
                          amplitude * 2.0f - amplitude;
                    dy += m_perlin.noise(nx * frequency + 5.2f, ny * frequency + 1.3f, 0.5f + i) * 
                          amplitude * 2.0f - amplitude;
                    amplitude *= 0.5f;
                    frequency *= 2.0f;
                }
            }
            
            // 计算源坐标
            float srcX = x + dx * warp.strength;
            float srcY = y + dy * warp.strength;
            
            // 双线性插值
            srcX = std::clamp(srcX, 0.0f, static_cast<float>(width - 1));
            srcY = std::clamp(srcY, 0.0f, static_cast<float>(height - 1));
            
            int x1 = static_cast<int>(srcX);
            int y1 = static_cast<int>(srcY);
            int x2 = std::min(x1 + 1, static_cast<int>(width - 1));
            int y2 = std::min(y1 + 1, static_cast<int>(height - 1));
            
            float tx = srcX - x1;
            float ty = srcY - y1;
            
            float v1 = heightmap[y1 * width + x1];
            float v2 = heightmap[y1 * width + x2];
            float v3 = heightmap[y2 * width + x1];
            float v4 = heightmap[y2 * width + x2];
            
            float vx1 = v1 * (1 - tx) + v2 * tx;
            float vx2 = v3 * (1 - tx) + v4 * tx;
            
            warped[y * width + x] = vx1 * (1 - ty) + vx2 * ty;
        });
        
        heightmap = std::move(warped);
    }
    
    void applySmoothing(HeightMap& heightmap, uint32_t width, uint32_t height,
                       uint32_t radius) {
        HeightMap smoothed = heightmap;
        
        // 并行平滑
        m_parallelProcessor->parallelFor2DChunked(width, height, 64,
            [&](uint32_t startX, uint32_t startY, uint32_t endX, uint32_t endY) {
                for (uint32_t y = startY; y < endY; ++y) {
                    for (uint32_t x = startX; x < endX; ++x) {
                        // 边界检查
                        if (x < radius || x >= width - radius || 
                            y < radius || y >= height - radius) {
                            continue;
                        }
                        
                        float sum = 0.0f;
                        int count = 0;
                        
                        for (int dy = -static_cast<int>(radius); dy <= static_cast<int>(radius); dy++) {
                            for (int dx = -static_cast<int>(radius); dx <= static_cast<int>(radius); dx++) {
                                sum += heightmap[(y + dy) * width + (x + dx)];
                                count++;
                            }
                        }
                        
                        smoothed[y * width + x] = sum / count;
                    }
                }
            });
        
        heightmap = std::move(smoothed);
    }
    
    HeightMap generateLayeredNoise(uint32_t width, uint32_t height,
                                  const std::vector<NoiseParams::NoiseLayer>& layers) {
        HeightMap result(width * height, 0.0f);
        
        for (const auto& layer : layers) {
            // Create a NoiseParams from the layer data
            NoiseParams params;
            params.scale = layer.scale;
            params.octaves = layer.octaves;
            params.persistence = layer.persistence;
            params.lacunarity = layer.lacunarity;
            params.type = layer.type;
            params.islandMode = layer.islandMode;
            
            HeightMap layerNoise = generateNoise(width, height, params);
            
            // 混合层
            for (size_t i = 0; i < result.size(); i++) {
                result[i] += layerNoise[i] * layer.weight;
            }
        }
        
        // 归一化
        float maxVal = *std::max_element(result.begin(), result.end());
        float minVal = *std::min_element(result.begin(), result.end());
        
        if (maxVal > minVal) {
            for (auto& val : result) {
                val = (val - minVal) / (maxVal - minVal);
            }
        }
        
        return result;
    }

    HeightMap generateRidgeNoise(uint32_t width, uint32_t height,
                                const NoiseParams& params) {
        // 先生成基础噪声
        NoiseParams baseParams = params;
        baseParams.type = NoiseType::PERLIN;
        HeightMap baseNoise = generateNoise(width, height, baseParams);
        
        // 转换为山脊噪声：1 - |noise|
        for (auto& val : baseNoise) {
            val = 1.0f - fabs(val - 0.5f) * 2.0f;
            if (params.ridgeWeight > 0.0f) {
                val = std::pow(val, params.ridgeWeight);
            }
        }
        
        return baseNoise;
    }
    
    HeightMap generateTerraceNoise(uint32_t width, uint32_t height,
                                  const NoiseParams& params) {
        // 先生成基础噪声
        NoiseParams baseParams = params;
        baseParams.type = NoiseType::PERLIN;
        HeightMap baseNoise = generateNoise(width, height, baseParams);
        
        // 应用梯田效果
        if (params.terraceLevels > 0) {
            float step = 1.0f / params.terraceLevels;
            for (auto& val : baseNoise) {
                val = floor(val / step) * step + step / 2.0f;
            }
        }
        
        return baseNoise;
    }
    
    HeightMap generateWorleyNoise(uint32_t width, uint32_t height,
                                 const NoiseParams& params) {
        WorleyNoise worley(m_seed);
        HeightMap result(width * height);
        
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                float nx = x / params.scale;
                float ny = y / params.scale;
                
                float value = worley.noise(nx, ny, 0);
                
                // 应用倍频
                if (params.octaves > 1) {
                    float amplitude = params.persistence;
                    float frequency = params.lacunarity;
                    
                    for (int i = 1; i < params.octaves; i++) {
                        value += worley.noise(nx * frequency, ny * frequency, i) * amplitude;
                        amplitude *= params.persistence;
                        frequency *= params.lacunarity;
                    }
                }
                
                result[y * width + x] = value;
            }
        }
        
        return result;
    }
    
    void applyDomainWarp(HeightMap& heightmap, uint32_t width, uint32_t height,
                        const NoiseParams::DomainWarp& warp) {
        if (!warp.enabled) return;
        
        HeightMap warped = heightmap;
        
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                float nx = x / warp.frequency;
                float ny = y / warp.frequency;
                
                // 计算扭曲偏移
                float dx = m_perlin.noise(nx, ny, 0.5f) * 2.0f - 1.0f;
                float dy = m_perlin.noise(nx + 5.2f, ny + 1.3f, 0.5f) * 2.0f - 1.0f;
                
                // 应用倍频扭曲
                if (warp.octaves > 1) {
                    float amplitude = 0.5f;
                    float frequency = 2.0f;
                    
                    for (uint32_t i = 1; i < warp.octaves; i++) {
                        dx += m_perlin.noise(nx * frequency, ny * frequency, 0.5f + i) * 
                              amplitude * 2.0f - amplitude;
                        dy += m_perlin.noise(nx * frequency + 5.2f, ny * frequency + 1.3f, 0.5f + i) * 
                              amplitude * 2.0f - amplitude;
                        amplitude *= 0.5f;
                        frequency *= 2.0f;
                    }
                }
                
                // 计算源坐标
                float srcX = x + dx * warp.strength;
                float srcY = y + dy * warp.strength;
                
                // 双线性插值
                srcX = std::clamp(srcX, 0.0f, static_cast<float>(width - 1));
                srcY = std::clamp(srcY, 0.0f, static_cast<float>(height - 1));
                
                int x1 = static_cast<int>(srcX);
                int y1 = static_cast<int>(srcY);
                int x2 = std::min(x1 + 1, static_cast<int>(width - 1));
                int y2 = std::min(y1 + 1, static_cast<int>(height - 1));
                
                float tx = srcX - x1;
                float ty = srcY - y1;
                
                float v1 = heightmap[y1 * width + x1];
                float v2 = heightmap[y1 * width + x2];
                float v3 = heightmap[y2 * width + x1];
                float v4 = heightmap[y2 * width + x2];
                
                float vx1 = v1 * (1 - tx) + v2 * tx;
                float vx2 = v3 * (1 - tx) + v4 * tx;
                
                warped[y * width + x] = vx1 * (1 - ty) + vx2 * ty;
            }
        }
        
        heightmap = std::move(warped);
    }
    
    void applyErosion(HeightMap& heightmap, uint32_t width, uint32_t height,
                     const ErosionParams& params) {
        if (params.hydraulicErosion) {
            applyHydraulicErosion(heightmap, width, height, params);
        }
        
        if (params.thermalErosion) {
            applyThermalErosion(heightmap, width, height, params);
        }
    }
    
    void applyTerracing(HeightMap& heightmap, uint32_t width, uint32_t height,
                       uint32_t levels) {
        if (levels == 0) return;
        
        float step = 1.0f / levels;
        
        for (auto& val : heightmap) {
            val = floor(val / step) * step + step / 2.0f;
        }
    }

    float applyPerlinNoise(float x, float y, float z) {
        return m_perlin.noise(x, y, z);
    }

    
private:
    void generatePerlinNoise(HeightMap& result, uint32_t width, uint32_t height,
                            const NoiseParams& params) {
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                float nx = x / params.scale;
                float ny = y / params.scale;
                
                float value = 0.0f;
                float amplitude = 1.0f;
                float frequency = 1.0f;
                float maxValue = 0.0f;
                
                for (int i = 0; i < params.octaves; i++) {
                    value += m_perlin.noise(nx * frequency, ny * frequency) * amplitude;
                    maxValue += amplitude;
                    amplitude *= params.persistence;
                    frequency *= params.lacunarity;
                }
                
                if (maxValue > 0) {
                    value /= maxValue;
                }
                
                result[y * width + x] = value;
            }
        }
    }
    
    void generateSimplexNoise(HeightMap& result, uint32_t width, uint32_t height,
                             const NoiseParams& params) {
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                float nx = x / params.scale;
                float ny = y / params.scale;
                
                float value = 0.0f;
                float amplitude = 1.0f;
                float frequency = 1.0f;
                float maxValue = 0.0f;
                
                for (int i = 0; i < params.octaves; i++) {
                    value += (m_simplex.noise(nx * frequency, ny * frequency) + 1.0f) * 0.5f * amplitude;
                    maxValue += amplitude;
                    amplitude *= params.persistence;
                    frequency *= params.lacunarity;
                }
                
                if (maxValue > 0) {
                    value /= maxValue;
                }
                
                result[y * width + x] = value;
            }
        }
    }
    
    void generateValueNoise(HeightMap& result, uint32_t width, uint32_t height,
                           const NoiseParams& params) {
        // 生成值噪声网格
        int gridSize = static_cast<int>(std::max(width, height) / params.scale) + 1;
        std::vector<float> grid(gridSize * gridSize);
        
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        for (auto& val : grid) {
            val = dist(m_rng);
        }
        
        // 插值生成值噪声
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                float gx = (x / params.scale) * (gridSize - 1);
                float gy = (y / params.scale) * (gridSize - 1);
                
                int x1 = static_cast<int>(gx);
                int y1 = static_cast<int>(gy);
                int x2 = std::min(x1 + 1, gridSize - 1);
                int y2 = std::min(y1 + 1, gridSize - 1);
                
                float tx = gx - x1;
                float ty = gy - y1;
                
                float v1 = grid[y1 * gridSize + x1];
                float v2 = grid[y1 * gridSize + x2];
                float v3 = grid[y2 * gridSize + x1];
                float v4 = grid[y2 * gridSize + x2];
                
                // 双线性插值
                float vx1 = v1 * (1 - tx) + v2 * tx;
                float vx2 = v3 * (1 - tx) + v4 * tx;
                
                result[y * width + x] = vx1 * (1 - ty) + vx2 * ty;
            }
        }
    }
    
    void applyNoisePostProcessing(HeightMap& noise, uint32_t width, uint32_t height,
                                 const NoiseParams& params) {
        // 应用岛模式
        if (params.islandMode) {
            applyIslandMode(noise, width, height);
        }
        
        // 应用域扭曲
        if (params.domainWarp.enabled) {
            applyDomainWarp(noise, width, height, params.domainWarp);
        }
        
        // 应用侵蚀
        if (params.erosionIterations > 0) {
            ErosionParams erosionParams;
            erosionParams.iterations = params.erosionIterations;
            applyErosion(noise, width, height, erosionParams);
        }
    }
    
    void applyIslandMode(HeightMap& noise, uint32_t width, uint32_t height) {
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                float dx = (x / static_cast<float>(width)) - 0.5f;
                float dy = (y / static_cast<float>(height)) - 0.5f;
                float distance = sqrt(dx * dx + dy * dy) * 2.0f;
                
                float falloff = 1.0f - distance;
                falloff = std::max(0.0f, falloff);
                
                noise[y * width + x] *= falloff;
            }
        }
    }
    
    void applyHydraulicErosion(HeightMap& heightmap, uint32_t width, uint32_t height,
                              const ErosionParams& params) {
        std::vector<float> water(heightmap.size(), 0.0f);
        std::vector<float> sediment(heightmap.size(), 0.0f);
        
        for (uint32_t iter = 0; iter < params.iterations; iter++) {
            // 模拟降雨
            for (auto& w : water) {
                w += params.rainAmount;
            }
            
            // 模拟水流和侵蚀
            for (uint32_t y = 1; y < height - 1; y++) {
                for (uint32_t x = 1; x < width - 1; x++) {
                    size_t idx = y * width + x;
                    
                    // 计算水流方向
                    float h = heightmap[idx] + water[idx];
                    float lowest = h;
                    int bestDx = 0, bestDy = 0;
                    
                    // 检查8个方向
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            if (dx == 0 && dy == 0) continue;
                            
                            size_t nIdx = (y + dy) * width + (x + dx);
                            float nh = heightmap[nIdx] + water[nIdx];
                            
                            if (nh < lowest) {
                                lowest = nh;
                                bestDx = dx;
                                bestDy = dy;
                            }
                        }
                    }
                    
                    if (bestDx != 0 || bestDy != 0) {
                        size_t flowIdx = (y + bestDy) * width + (x + bestDx);
                        
                        // 计算水流强度
                        float deltaH = h - lowest;
                        if (deltaH > params.minSlope) {
                            float flow = std::min(water[idx], deltaH * params.pipeLength);
                            
                            // 搬运沉积物
                            float sedimentCapacity = flow * params.sedimentCapacity;
                            float actualSediment = std::min(sediment[idx], sedimentCapacity);
                            
                            // 侵蚀
                            float erosion = (sedimentCapacity - actualSediment) * params.erosionRate;
                            heightmap[idx] -= erosion;
                            sediment[idx] += erosion + actualSediment;
                            
                            // 水流和沉积物转移
                            water[idx] -= flow;
                            water[flowIdx] += flow * (1.0f - params.evaporationRate);
                            sediment[idx] -= actualSediment;
                            sediment[flowIdx] += actualSediment * (1.0f - params.depositionRate);
                        }
                    }
                    
                    // 蒸发
                    water[idx] *= (1.0f - params.evaporationRate);
                    
                    // 沉积
                    float deposit = sediment[idx] * params.depositionRate;
                    heightmap[idx] += deposit;
                    sediment[idx] -= deposit;
                }
            }
        }
    }
    
    void applyThermalErosion(HeightMap& heightmap, uint32_t width, uint32_t height,
                            const ErosionParams& params) {
        std::vector<float> changes(heightmap.size(), 0.0f);
        
        for (uint32_t iter = 0; iter < params.iterations; iter++) {
            for (uint32_t y = 1; y < height - 1; y++) {
                for (uint32_t x = 1; x < width - 1; x++) {
                    size_t idx = y * width + x;
                    float h = heightmap[idx];
                    
                    // 计算坡度
                    float maxSlope = 0.0f;
                    float totalDiff = 0.0f;
                    int count = 0;
                    
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            if (dx == 0 && dy == 0) continue;
                            
                            size_t nIdx = (y + dy) * width + (x + dx);
                            float nh = heightmap[nIdx];
                            float slope = h - nh;
                            
                            if (slope > maxSlope) {
                                maxSlope = slope;
                            }
                            
                            if (slope > params.talusAngle * (M_PI / 180.0f) * params.pipeLength) {
                                totalDiff += slope;
                                count++;
                            }
                        }
                    }
                    
                    // 应用热侵蚀
                    if (count > 0 && maxSlope > params.talusAngle * (M_PI / 180.0f)) {
                        float erosion = totalDiff * params.thermalRate / count;
                        changes[idx] -= erosion;
                        
                        // 分配到邻居
                        for (int dy = -1; dy <= 1; dy++) {
                            for (int dx = -1; dx <= 1; dx++) {
                                if (dx == 0 && dy == 0) continue;
                                
                                size_t nIdx = (y + dy) * width + (x + dx);
                                float slope = h - heightmap[nIdx];
                                
                                if (slope > 0) {
                                    changes[nIdx] += erosion * (slope / totalDiff);
                                }
                            }
                        }
                    }
                }
            }
            
            // 应用变化
            for (size_t i = 0; i < heightmap.size(); i++) {
                heightmap[i] += changes[i];
                changes[i] = 0.0f;
            }
        }
    }
};

// 静态成员初始化
const int PerlinNoiseImp::permutation[256] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,
    175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,
    229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
    102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,
    89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,
    173,186,3,64,52,217,226,250,124,123,5,202,38,147,118,126,
    255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
    223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,
    155,167,43,172,9,129,22,39,253,19,98,108,110,79,113,224,
    232,178,185,112,104,218,246,97,228,251,34,242,193,238,210,
    144,12,191,179,162,241,81,51,145,235,249,14,239,107,49,
    192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,
    127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,
    128,195,78,66,215,61,156,180
};

const int SimplexNoiseImpl::grad3[12][3] = {
    {1,1,0}, {-1,1,0}, {1,-1,0}, {-1,-1,0},
    {1,0,1}, {-1,0,1}, {1,0,-1}, {-1,0,-1},
    {0,1,1}, {0,-1,1}, {0,1,-1}, {0,-1,-1}
};

const int SimplexNoiseImpl::grad4[32][4] = {
    {0,1,1,1}, {0,1,1,-1}, {0,1,-1,1}, {0,1,-1,-1},
    {0,-1,1,1}, {0,-1,1,-1}, {0,-1,-1,1}, {0,-1,-1,-1},
    {1,0,1,1}, {1,0,1,-1}, {1,0,-1,1}, {1,0,-1,-1},
    {-1,0,1,1}, {-1,0,1,-1}, {-1,0,-1,1}, {-1,0,-1,-1},
    {1,1,0,1}, {1,1,0,-1}, {1,-1,0,1}, {1,-1,0,-1},
    {-1,1,0,1}, {-1,1,0,-1}, {-1,-1,0,1}, {-1,-1,0,-1},
    {1,1,1,0}, {1,1,-1,0}, {1,-1,1,0}, {1,-1,-1,0},
    {-1,1,1,0}, {-1,1,-1,0}, {-1,-1,1,0}, {-1,-1,-1,0}
};

const int SimplexNoiseImpl::simplex[64][4] = {
    {0,1,2,3}, {0,1,3,2}, {0,0,0,0}, {0,2,3,1}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {1,2,3,0},
    {0,2,1,3}, {0,0,0,0}, {0,3,1,2}, {0,3,2,1}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {1,3,2,0},
    {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0},
    {1,2,0,3}, {0,0,0,0}, {1,3,0,2}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {2,3,0,1}, {2,3,1,0},
    {1,0,2,3}, {1,0,3,2}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {2,0,3,1}, {0,0,0,0}, {2,1,3,0},
    {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0},
    {2,0,1,3}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {3,0,1,2}, {3,0,2,1}, {0,0,0,0}, {3,1,2,0},
    {2,1,0,3}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {3,1,0,2}, {0,0,0,0}, {3,2,0,1}, {3,2,1,0}
};

// NoiseGenerator公共接口实现
NoiseGenerator::NoiseGenerator(uint32_t seed) 
    : m_impl(std::make_unique<Impl>(seed)) {
}

NoiseGenerator::~NoiseGenerator() = default;

HeightMap NoiseGenerator::generateHeightMap(uint32_t width, uint32_t height, 
                                           const NoiseParams& params) {
    // 如果有分层，使用分层噪声
    if (!params.layers.empty()) {
        return generateLayeredNoise(width, height, params.layers);
    }
    
    // 否则生成基本噪声
    return generateNoise(width, height, params);
}

HeightMap NoiseGenerator::generateNoise(uint32_t width, uint32_t height,
                                       const NoiseParams& params) {
    return m_impl->generateNoise(width, height, params);
}

HeightMap NoiseGenerator::generateLayeredNoise(uint32_t width, uint32_t height,
                                              const std::vector<NoiseParams::NoiseLayer>& layers) {
    return m_impl->generateLayeredNoise(width, height, layers);
}

HeightMap NoiseGenerator::generateRidgeNoise(uint32_t width, uint32_t height,
                                            const NoiseParams& params) {
    return m_impl->generateRidgeNoise(width, height, params);
}

HeightMap NoiseGenerator::generateTerraceNoise(uint32_t width, uint32_t height,
                                              const NoiseParams& params) {
    return m_impl->generateTerraceNoise(width, height, params);
}

HeightMap NoiseGenerator::generateWorleyNoise(uint32_t width, uint32_t height,
                                             const NoiseParams& params) {
    return m_impl->generateWorleyNoise(width, height, params);
}

void NoiseGenerator::applyDomainWarp(HeightMap& heightmap, uint32_t width, uint32_t height,
                                    const NoiseParams::DomainWarp& warp) {
    m_impl->applyDomainWarp(heightmap, width, height, warp);
}

void NoiseGenerator::applyErosion(HeightMap& heightmap, uint32_t width, uint32_t height,
                                 const ErosionParams& params) {
    m_impl->applyErosion(heightmap, width, height, params);
}

void NoiseGenerator::applySmoothing(HeightMap& heightmap, uint32_t width, uint32_t height,
                                   uint32_t radius) {
    m_impl->applySmoothing(heightmap, width, height, radius);
}

void NoiseGenerator::applyTerracing(HeightMap& heightmap, uint32_t width, uint32_t height,
                                   uint32_t levels) {
    m_impl->applyTerracing(heightmap, width, height, levels);
}

float NoiseGenerator::applyPerlinNoise(float x, float y, float z)
{
    return m_impl->applyPerlinNoise(x, y, z);
}

} // namespace internal
} // namespace MapGenerator

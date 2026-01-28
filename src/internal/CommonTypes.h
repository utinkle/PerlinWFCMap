// src/internal/CommonTypes.h
#ifndef MAPGENERATOR_INTERNAL_COMMONTYPES_H
#define MAPGENERATOR_INTERNAL_COMMONTYPES_H

#include "MapGenerator.h"
#include <vector>
#include <cstdint>

namespace MapGenerator {
namespace internal {

// 噪声类型
enum class NoiseType {
    PERLIN,
    SIMPLEX,
    VALUE,
    WORLEY
};

// 噪声参数
struct NoiseParams {
    float scale = 100.0f;
    int32_t octaves = 6;
    float persistence = 0.5f;
    float lacunarity = 2.0f;
    NoiseType type = NoiseType::PERLIN;
    bool islandMode = false;
    uint32_t erosionIterations = 5;
    float warpStrength = 0.5f;
    float warpFrequency = 0.1f;
    float ridgeWeight = 0.0f;
    float terraceLevels = 0.0f;
    
    // 域扭曲参数
    struct DomainWarp {
        bool enabled = false;
        float strength = 30.0f;
        float frequency = 0.05f;
        uint32_t octaves = 3;
    } domainWarp;
    
    // 混合噪声 - 只包含权重和必要参数，避免递归定义
    struct NoiseLayer {
        float weight = 1.0f;
        float scale = 100.0f;
        int32_t octaves = 6;
        float persistence = 0.5f;
        float lacunarity = 2.0f;
        NoiseType type = NoiseType::PERLIN;
        bool islandMode = false;
    };
    std::vector<NoiseLayer> layers;
};

// WFC参数
struct WFCParams {
    uint32_t iterations = 1000;
    float entropyWeight = 0.1f;
    bool enableBacktracking = true;
    uint32_t maxBacktrackDepth = 100;
    float temperature = 1.0f; // 模拟退火温度
    bool useWeights = true;
    bool propagateDiagonally = false;
    uint32_t patternSize = 2; // NxN模式大小
    
    // 高级参数
    bool allowRotations = true;
    bool allowReflections = false;
    float minEntropyThreshold = 0.001f;
    uint32_t superpositionSize = 10; // 叠加态数量
    bool useManualRules = false;
    
    // 输出控制
    bool failOnContradiction = true;
    uint32_t retryCount = 3;
};

// 侵蚀参数
struct ErosionParams {
    uint32_t iterations = 10;
    float rainAmount = 0.01f;
    float evaporationRate = 0.01f;
    float sedimentCapacity = 0.1f;
    float depositionRate = 0.3f;
    float erosionRate = 0.3f;
    float gravity = 9.8f;
    float waterLevel = 0.0f;
    
    // 热侵蚀
    bool thermalErosion = false;
    float talusAngle = 30.0f;
    float thermalRate = 0.1f;
    
    // 水力侵蚀
    bool hydraulicErosion = true;
    uint32_t dropletLifetime = 30;
    float inertia = 0.05f;
    float minSlope = 0.01f;
    float pipeLength = 1.0f;
};

// 河流参数
struct RiverParams {
    uint32_t count = 50;
    float minSourceHeight = 0.6f;
    float maxSourceHeight = 0.9f;
    float minRiverLength = 10.0f;
    float maxRiverLength = 100.0f;
    float riverWidth = 1.5f;
    float meanderAmplitude = 2.0f;
    float meanderWavelength = 10.0f;
    bool tributaries = true;
    float minTributaryAngle = 30.0f;
    float maxTributaryAngle = 60.0f;
    
    // 湖泊生成
    bool generateLakes = true;
    float lakeProbability = 0.3f;
    float minLakeSize = 3.0f;
    float maxLakeSize = 20.0f;
};

// 生物群落参数
struct BiomeParams {
    float temperatureScale = 100.0f;
    float moistureScale = 100.0f;
    float temperatureBias = 0.0f;
    float moistureBias = 0.0f;
    
    // 生物群落边界
    float desertThreshold = 0.7f;
    float savannaThreshold = 0.5f;
    float forestThreshold = 0.3f;
    float taigaThreshold = 0.1f;
    float tundraThreshold = -0.1f;
    
    // 高度影响
    float heightInfluence = 0.3f;
    float equatorialBelt = 0.3f; // 赤道带宽度的比例
};

// 装饰参数
struct DecorationParams {
    // 树木
    float treeDensity = 0.3f;
    float treeClusterSize = 5.0f;
    float treeClusterChance = 0.7f;
    
    // 岩石
    float rockDensity = 0.1f;
    float rockClusterSize = 3.0f;
    float rockOnSlopeBias = 0.8f;
    
    // 植被
    float grassDensity = 0.6f;
    float bushDensity = 0.2f;
    float flowerDensity = 0.05f;
    
    // 分布控制
    float elevationBias = 0.5f;
    float slopeBias = 0.3f;
    float moistureBias = 0.7f;
    
    // 最小间距
    float minTreeSpacing = 2.0f;
    float minRockSpacing = 1.5f;
    float minDecorationSpacing = 0.5f;
};

} // namespace internal
} // namespace MapGenerator

#endif // MAPGENERATOR_INTERNAL_COMMONTYPES_H
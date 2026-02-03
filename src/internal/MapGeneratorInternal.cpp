#define _USE_MATH_DEFINES
#include "MapGeneratorInternal.h"
#include "ParallelUtils.h"
#include "NoiseGenerator.h"
#include "ThreadPool.h"
#include <algorithm>
#include <chrono>
#include <memory>
#include <random>
#include <future>
#include <iostream>
#include <cmath>
#include <queue>
#include <stack>

namespace MapGenerator {
namespace internal {

struct RiverPoint {
    uint32_t x;
    uint32_t y;
    float height;
    bool isTributary;
    uint32_t depth; // 递归深度
};

class MapGeneratorInternal::Impl {
private:
    std::mt19937 m_rng;
    uint32_t m_seed;
    std::unique_ptr<NoiseGenerator> m_noiseGen;
    std::unique_ptr<ThreadPool> m_threadPool;
    std::unique_ptr<ParallelProcessor> m_parallelProcessor;

    // 缓存
    std::unordered_map<uint64_t, std::shared_ptr<MapData>> m_cache;
    
public:
    Impl(uint32_t seed) 
        : m_seed(seed), m_rng(seed),
          m_noiseGen(std::make_unique<NoiseGenerator>(seed)),
          m_threadPool(std::make_unique<ThreadPool>(std::thread::hardware_concurrency())) {
    }
    
    std::shared_ptr<MapData> generate(const MapConfig& config) {
        // 检查缓存
        uint64_t cacheKey = computeCacheKey(config);
        auto it = m_cache.find(cacheKey);
        if (it != m_cache.end()) {
            return it->second;
        }

        if (config.threadCount > 0) {
            m_parallelProcessor = std::make_unique<ParallelProcessor>(config.threadCount);
        }
        
        auto startTime = std::chrono::high_resolution_clock::now();
        
        auto data = std::make_shared<MapData>();
        data->config = config;
        
        // 步骤1: 生成高度图
        data->heightMap = generateHeightmapOnly(config);
        
        // 步骤2: 应用侵蚀
        ErosionParams erosionParams;
        erosionParams.iterations = 5;
        erosionParams.thermalErosion = true;
        erosionParams.hydraulicErosion = true;
        erosionParams.talusAngle = 35.0f;

        applyErosion(data->heightMap, config, erosionParams);

        // 步骤3: 平滑高度图
        m_noiseGen->applySmoothing(data->heightMap, config.width, config.height, 1);

        // 步骤4: 生成地形图
        data->terrainMap = generateTerrainOnly(data->heightMap, config);

        // 步骤5: 生成河流
        RiverParams riverParams;
        riverParams.count = static_cast<uint32_t>(config.width * config.height * 0.0005f);
        riverParams.minSourceHeight = 0.6f;
        riverParams.maxSourceHeight = 0.9f;

        generateRivers(data->terrainMap, data->heightMap, config, riverParams);
        
        // 步骤6: 计算统计信息
        calculateStatistics(*data);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        data->generationTimeMs = std::chrono::duration_cast<
            std::chrono::milliseconds>(endTime - startTime).count();
        
        // 缓存结果
        m_cache[cacheKey] = data;
        
        return data;
    }
    
    std::vector<std::shared_ptr<MapData>> generateBatch(
        const MapConfig& baseConfig, uint32_t count) {
        std::vector<std::shared_ptr<MapData>> results(count);
        std::vector<std::future<std::shared_ptr<MapData>>> futures;
        
        for (uint32_t i = 0; i < count; i++) {
            MapConfig config = baseConfig;
            config.seed = baseConfig.seed + i;
            
            futures.push_back(m_threadPool->enqueueTask([this, config]() {
                return this->generate(config);
            }));
        }
        
        for (uint32_t i = 0; i < count; i++) {
            results[i] = futures[i].get();
        }
        
        return results;
    }
    
    HeightMap generateHeightmapOnly(const MapConfig& config) {
        NoiseParams noiseParams = createNoiseParamsFromConfig(config);
        
        // 根据预设选择噪声类型
        switch (config.preset) {
            case MapConfig::Preset::MOUNTAINS:
            case MapConfig::Preset::ALPINE:
                noiseParams.ridgeWeight = 2.0f;  // 增加山脊效果
                noiseParams.type = NoiseType::PERLIN;  // 基础噪声类型还是Perlin
                break;
            case MapConfig::Preset::DESERT_CANYONS:
                // 使用梯田噪声处理
                noiseParams.terraceLevels = 8.0f;  // 设置梯田级别
                noiseParams.type = NoiseType::PERLIN;
                break;
            case MapConfig::Preset::ARCHIPELAGO:
                // 使用细胞噪声
                noiseParams.type = NoiseType::WORLEY;
                break;
            default:
                noiseParams.type = NoiseType::PERLIN;
                break;
        }
        
        // 应用气候影响
        applyClimateEffects(noiseParams, config.climate, 
                           config.temperature, config.humidity);
        
        // 并行生成高度图
        HeightMap heightmap(config.width * config.height);
        
        // 根据地图大小决定是否使用并行
        if (config.width * config.height >= 256 * 256 && config.threadCount > 1) {
            // 并行生成噪声
            generateNoiseParallel(heightmap, config.width, config.height, noiseParams);
        } else {
            // 小地图串行生成
            heightmap = m_noiseGen->generateHeightMap(config.width, config.height, noiseParams);
        }
        
        return heightmap;
    }

    // 并行生成噪声
    void generateNoiseParallel(HeightMap& heightmap, uint32_t width, uint32_t height,
                              const NoiseParams& params) {
        heightmap = m_noiseGen->generateNoise(width, height, params);
    }

    // 优化地形生成
    TileMap generateTerrainOnly(const HeightMap& heightmap, const MapConfig& config) {
        TileMap terrainMap(heightmap.size());
        
        // 创建生物群落参数（线程安全）
        BiomeParams biomeParams = createBiomeParams(config);
        
        // 并行处理每个像素
        m_parallelProcessor->processHeightMapParallel(heightmap, config.width, config.height,
            [&](uint32_t x, uint32_t y, float height) {
                uint32_t idx = y * config.width + x;
                
                // 计算生物群落参数（并行安全）
                float temperature = calculateTemperature(x, y, config, height, biomeParams);
                float moisture = calculateMoisture(x, y, config, height, biomeParams);
                
                // 确定地形类型
                TerrainType terrain = determineTerrainType(height, temperature, moisture, config);
                terrainMap[idx] = static_cast<uint32_t>(terrain);
            });
        
        return terrainMap;
    }
    
    // 优化侵蚀应用
    void applyErosion(HeightMap& heightmap, const MapConfig& config,
                     const ErosionParams& params) {
        if (params.hydraulicErosion) {
            applyHydraulicErosionParallel(heightmap, config.width, config.height, params);
        }
        
        if (params.thermalErosion) {
            applyThermalErosionParallel(heightmap, config.width, config.height, params);
        }
        
        // 并行重新归一化高度图
        normalizeHeightmapParallel(heightmap);
    }

    // 并行水力侵蚀
    void applyHydraulicErosionParallel(HeightMap& heightmap, uint32_t width, uint32_t height,
                                      const ErosionParams& params) {
        
        std::vector<float> water(heightmap.size(), 0.0f);
        std::vector<float> sediment(heightmap.size(), 0.0f);
        
        // 为每个线程创建本地缓冲区以避免竞争
        const uint32_t chunkSize = 32;
        
        for (uint32_t iter = 0; iter < params.iterations; iter++) {
            // 模拟降雨（并行）
            m_parallelProcessor->parallelFor2DChunked(width, height, chunkSize,
                [&](uint32_t startX, uint32_t startY, uint32_t endX, uint32_t endY) {
                    for (uint32_t y = startY; y < endY; ++y) {
                        for (uint32_t x = startX; x < endX; ++x) {
                            uint32_t idx = y * width + x;
                            water[idx] += params.rainAmount;
                        }
                    }
                });
            
            // 分块处理侵蚀，每个块独立处理以避免数据竞争
            m_parallelProcessor->parallelFor2DChunked(width, height, chunkSize,
                [&](uint32_t startX, uint32_t startY, uint32_t endX, uint32_t endY) {
                    // 限制边界
                    uint32_t realStartX = std::max(startX, 1u);
                    uint32_t realStartY = std::max(startY, 1u);
                    uint32_t realEndX = std::min(endX, width - 1);
                    uint32_t realEndY = std::min(endY, height - 1);
                    
                    for (uint32_t y = realStartY; y < realEndY; ++y) {
                        for (uint32_t x = realStartX; x < realEndX; ++x) {
                            applyHydraulicErosionAtPoint(heightmap, water, sediment,
                                                        x, y, width, height, params);
                        }
                    }
                });
        }
    }
    
    // 单点水力侵蚀（线程安全）
    void applyHydraulicErosionAtPoint(HeightMap& heightmap,
                                     std::vector<float>& water,
                                     std::vector<float>& sediment,
                                     uint32_t x, uint32_t y,
                                     uint32_t width, uint32_t height,
                                     const ErosionParams& params) {
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
    
    // 并行热侵蚀
    void applyThermalErosionParallel(HeightMap& heightmap, uint32_t width, uint32_t height,
                                    const ErosionParams& params) {
        
        std::vector<float> changes(heightmap.size(), 0.0f);
        
        for (uint32_t iter = 0; iter < params.iterations; iter++) {
            // 使用线程安全的处理方式
            std::vector<float> localChanges(heightmap.size(), 0.0f);
            
            m_parallelProcessor->parallelFor2DChunked(width, height, 32,
                [&](uint32_t startX, uint32_t startY, uint32_t endX, uint32_t endY) {
                    uint32_t realStartX = std::max(startX, 1u);
                    uint32_t realStartY = std::max(startY, 1u);
                    uint32_t realEndX = std::min(endX, width - 1);
                    uint32_t realEndY = std::min(endY, height - 1);
                    
                    for (uint32_t y = realStartY; y < realEndY; ++y) {
                        for (uint32_t x = realStartX; x < realEndX; ++x) {
                            applyThermalErosionAtPoint(heightmap, localChanges,
                                                      x, y, width, height, params);
                        }
                    }
                });
            
            // 合并变化
            for (size_t i = 0; i < heightmap.size(); ++i) {
                heightmap[i] += localChanges[i];
            }
        }
    }
    
    // 单点热侵蚀（线程安全）
    void applyThermalErosionAtPoint(HeightMap& heightmap,
                                   std::vector<float>& changes,
                                   uint32_t x, uint32_t y,
                                   uint32_t width, uint32_t height,
                                   const ErosionParams& params) {
        
        size_t idx = y * width + x;
        float h = heightmap[idx];
        
        // 计算坡度和总差异
        float maxSlope = 0.0f;
        float totalDiff = 0.0f;
        std::vector<std::pair<size_t, float>> steepNeighbors;
        
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                
                size_t nIdx = (y + dy) * width + (x + dx);
                float nh = heightmap[nIdx];
                float slope = h - nh;
                
                if (slope > maxSlope) {
                    maxSlope = slope;
                }
                
                float talusThreshold = params.talusAngle * (M_PI / 180.0f) * params.pipeLength;
                if (slope > talusThreshold) {
                    totalDiff += slope;
                    steepNeighbors.emplace_back(nIdx, slope);
                }
            }
        }
        
        // 应用热侵蚀
        if (!steepNeighbors.empty() && maxSlope > params.talusAngle * (M_PI / 180.0f)) {
            float erosion = totalDiff * params.thermalRate / steepNeighbors.size();
            changes[idx] -= erosion;
            
            // 分配到邻居
            for (const auto& [neighborIdx, slope] : steepNeighbors) {
                if (totalDiff > 0) {
                    changes[neighborIdx] += erosion * (slope / totalDiff);
                }
            }
        }
    }
    
    // 并行归一化高度图
    void normalizeHeightmapParallel(HeightMap& heightmap) {
        if (heightmap.empty()) return;
        
        // 使用新的并行函数查找最小最大值
        auto [minVal, maxVal] = m_parallelProcessor->parallelMinMax(
            heightmap.data(), static_cast<uint32_t>(heightmap.size())
        );
        
        float range = maxVal - minVal;
        
        if (range > 0.0f) {
            // 使用新的并行函数进行归一化
            m_parallelProcessor->parallelNormalize(
                heightmap.data(), static_cast<uint32_t>(heightmap.size()),
                minVal, maxVal
            );
        } else {
            // 所有值相同，设为0.5
            std::fill(heightmap.begin(), heightmap.end(), 0.5f);
        }
    }
    
    void generateRivers(TileMap& terrainMap, const HeightMap& heightmap,
                       const MapConfig& config, const RiverParams& params) {
        // 生成河流网络
        generateRiverNetwork(terrainMap, heightmap, config, params);
        
        // 生成湖泊
        if (params.generateLakes) {
            generateLakesParallel(terrainMap, heightmap, config, params);
        }
    }
    
private:
    uint64_t computeCacheKey(const MapConfig& config) {
        // 简单的哈希函数用于缓存键
        uint64_t key = config.seed;
        key = key * 31 + config.width;
        key = key * 31 + config.height;
        key = key * 31 + static_cast<uint32_t>(config.preset);
        key = key * 31 + *reinterpret_cast<const uint32_t*>(&config.seaLevel);
        key = key * 31 + *reinterpret_cast<const uint32_t*>(&config.temperature);
        return key;
    }
    
    NoiseParams createNoiseParamsFromConfig(const MapConfig& config) {
        NoiseParams params;
        params.scale = config.noiseScale;
        params.octaves = config.noiseOctaves;
        params.persistence = config.noisePersistence;
        params.lacunarity = config.noiseLacunarity;
        params.islandMode = (config.preset == MapConfig::Preset::ISLANDS || 
                           config.preset == MapConfig::Preset::ARCHIPELAGO);
        params.erosionIterations = 5;
        
        // 根据预设调整参数
        switch (config.preset) {
            case MapConfig::Preset::ISLANDS:
                params.scale *= 0.8f;
                params.domainWarp.enabled = true;
                params.domainWarp.strength = 20.0f;
                break;
            case MapConfig::Preset::MOUNTAINS:
                params.persistence = 0.7f;
                params.lacunarity = 3.0f;
                params.erosionIterations = 10;
                break;
            case MapConfig::Preset::DESERT_CANYONS:
                params.persistence = 0.6f;
                params.lacunarity = 2.5f;
                params.erosionIterations = 15;
                break;
            case MapConfig::Preset::ALPINE:
                params.persistence = 0.8f;
                params.lacunarity = 3.0f;
                params.erosionIterations = 12;
                break;
            default:
                break;
        }
        
        return params;
    }
    
    DecorationParams createDecorationParamsFromConfig(const MapConfig& config) {
        DecorationParams params;
        
        // 根据气候调整装饰密度
        switch (config.climate) {
            case ClimateType::TROPICAL:
                params.treeDensity = 0.4f;
                params.grassDensity = 0.7f;
                params.bushDensity = 0.3f;
                break;
            case ClimateType::ARID:
                params.treeDensity = 0.05f;
                params.grassDensity = 0.2f;
                params.rockDensity = 0.3f;
                break;
            case ClimateType::POLAR:
                params.treeDensity = 0.1f;
                params.grassDensity = 0.3f;
                break;
            default:
                params.treeDensity = 0.3f;
                params.grassDensity = 0.6f;
                params.bushDensity = 0.2f;
                break;
        }
        
        // 根据地形预设调整
        switch (config.preset) {
        case MapConfig::Preset::ISLANDS:
            // 岛屿装饰
            break;
            case MapConfig::Preset::DESERT_CANYONS:
                params.rockDensity = 0.4f;
                params.rockOnSlopeBias = 0.9f;
                break;
            case MapConfig::Preset::SWAMP_LAKES:
                params.bushDensity = 0.4f;
                params.grassDensity = 0.4f;
                break;
            default:
                break;
        }
        
        return params;
    }
    
    void applyClimateEffects(NoiseParams& params, ClimateType climate,
                            float temperature, float humidity) {
        switch (climate) {
            case ClimateType::ARID:
                params.persistence *= 0.8f;
                params.scale *= 1.2f;
                break;
            case ClimateType::TROPICAL:
                params.persistence *= 1.2f;
                params.octaves += 1;
                break;
            case ClimateType::POLAR:
                params.persistence *= 0.7f;
                params.lacunarity *= 1.1f;
                break;
            default:
                break;
        }
        
        // 湿度和温度影响
        if (humidity > 0.7f) {
            params.persistence *= (1.0f + (humidity - 0.7f) * 0.5f);
        }
        
        if (temperature < 0.3f) {
            params.scale *= 1.1f;
        }
    }
    
    void generateTerrainChunk(const HeightMap& heightmap, TileMap& terrainMap,
                             const MapConfig& config, uint32_t startY, uint32_t endY) {
        BiomeParams biomeParams = createBiomeParams(config);
        
        for (uint32_t y = startY; y < endY; y++) {
            for (uint32_t x = 0; x < config.width; x++) {
                uint32_t idx = y * config.width + x;
                float height = heightmap[idx];
                
                // 计算生物群落参数
                float temperature = calculateTemperature(x, y, config, height, biomeParams);
                float moisture = calculateMoisture(x, y, config, height, biomeParams);
                
                // 确定地形类型
                TerrainType terrain = determineTerrainType(height, temperature, moisture, config);
                terrainMap[idx] = static_cast<uint32_t>(terrain);
            }
        }
    }
    
    BiomeParams createBiomeParams(const MapConfig& config) {
        BiomeParams params;
        params.temperatureScale = 100.0f;
        params.moistureScale = 100.0f;
        
        // 根据气候调整
        switch (config.climate) {
            case ClimateType::ARID:
                params.desertThreshold = 0.5f;
                params.moistureBias = -0.3f;
                break;
            case ClimateType::TROPICAL:
                params.forestThreshold = 0.5f;
                params.moistureBias = 0.3f;
                break;
            case ClimateType::POLAR:
                params.tundraThreshold = 0.3f;
                params.temperatureBias = -0.4f;
                break;
            default:
                break;
        }
        
        return params;
    }
    
    float calculateTemperature(uint32_t x, uint32_t y, const MapConfig& config,
                               float height, const BiomeParams& params) {
        // 1. 基础温度
        float temperature = config.temperature;

        // 2. 纬度影响 - 使用加法而不是乘法
        // 赤道附近更热，两极更冷
        float latitude = static_cast<float>(y) / config.height; // [0, 1]
        float latDistance = fabs(latitude - 0.5f); // 距离赤道的距离 [0, 0.5]

        // 纬度影响：赤道+0.2，两极-0.3
        float latEffect = 0.2f - latDistance * 1.0f; // [0.2, -0.3]

        // 3. 高度影响 - 每"升高"降温
        // height范围[0,1]，高处降温更多
        float heightEffect = -height * 0.3f; // [0, -0.3]

        // 4. 季节/日夜模拟（简化）
        float seasonalEffect = 0.0f;
        // 可以根据需要添加季节变化

        // 5. 使用柏林噪声生成空间变化
        float noiseX = x / params.temperatureScale;
        float noiseY = y / params.temperatureScale;

        // 多层噪声
        float noise1 = m_noiseGen->applyPerlinNoise(noiseX, noiseY, 0.5f); // 主噪声
        float noise2 = m_noiseGen->applyPerlinNoise(noiseX * 2.0f, noiseY * 2.0f, 1.5f) * 0.5f; // 细节
        float noise3 = m_noiseGen->applyPerlinNoise(noiseX * 0.3f, noiseY * 0.3f, 2.5f) * 0.8f; // 大尺度

        float totalNoise = (noise1 * 0.6f + noise2 * 0.3f + noise3 * 0.1f) * 0.3f; // [-0.3, 0.3]


        // 综合计算
        temperature = temperature + latEffect + heightEffect + totalNoise +
                      params.temperatureBias + seasonalEffect;

        // 确保在合理范围内
        return std::clamp(temperature, 0.0f, 1.0f);
    }

    float calculateMoisture(uint32_t x, uint32_t y, const MapConfig& config,
                            float height, const BiomeParams& params) {
        // 1. 基础湿度 - 直接使用配置值
        float moisture = config.humidity;

        // 2. 纬度影响 - 使用加法而不是乘法
        float latitude = static_cast<float>(y) / config.height;
        float latDistance = fabs(latitude - 0.5f); // 距离中心的距离 [0, 0.5]
        float latEffect = -latDistance * 0.3f; // 边缘比中心干燥 0.15

        // 3. 高度影响 - 适度干燥
        float heightEffect = -height * 0.2f; // 高处干燥，减少幅度

        // 4. 使用多层柏林噪声生成更丰富的湿度分布
        float noiseX = x / params.moistureScale;
        float noiseY = y / params.moistureScale;

        // 主噪声层
        float noise1 = m_noiseGen->applyPerlinNoise(noiseX, noiseY, 0.5f);

        // 细节噪声层
        float noise2 = m_noiseGen->applyPerlinNoise(noiseX * 2.0f, noiseY * 2.0f, 1.5f) * 0.5f;

        // 大尺度噪声层
        float noise3 = m_noiseGen->applyPerlinNoise(noiseX * 0.5f, noiseY * 0.5f, 2.5f) * 0.8f;

        // 综合噪声
        float totalNoise = (noise1 * 0.5f + noise2 * 0.3f + noise3 * 0.2f) * 0.4f;
        // 现在 totalNoise 范围大约是 [-0.4, 0.4]

        // 5. 风向/降水带影响 - 模拟降雨带
        float precipitationBand = 0.0f;
        float normalizedY = static_cast<float>(y) / config.height;

        // 在特定纬度增加湿度（模拟赤道降水带）
        if (normalizedY > 0.4f && normalizedY < 0.6f) {
            precipitationBand = 0.15f; // 赤道附近更湿润
        }
        // 中纬度也有一定降水
        else if ((normalizedY > 0.2f && normalizedY < 0.4f) ||
                 (normalizedY > 0.6f && normalizedY < 0.8f)) {
            precipitationBand = 0.08f;
        }

        // 6. 综合所有因素
        moisture = moisture + latEffect + heightEffect + totalNoise + precipitationBand + params.moistureBias;

        // 确保在合理范围内
        return std::clamp(moisture, 0.0f, 1.0f);
    }
    
    TerrainType determineTerrainType(float height, float temperature, float moisture,
                                    const MapConfig& config) {
        // 根据高度确定基础地形
        if (height < config.seaLevel) {
            if (height < config.seaLevel * 0.5f) {
                return TerrainType::DEEP_OCEAN;
            } else {
                return TerrainType::SHALLOW_OCEAN;
            }
        } else if (height < config.seaLevel + 0.02f) {
            return TerrainType::COAST;
        } else if (height < config.beachHeight) {
            return TerrainType::BEACH;
        }
        
        // 陆地地形
        if (height < config.plainHeight) {
            // 低地：根据气候决定
            if (temperature > 0.7f && moisture < 0.3f) {
                return TerrainType::DESERT;
            } else if (temperature > 0.6f && moisture > 0.7f) {
                return TerrainType::SWAMP;
            } else if (moisture > 0.5f) {
                return TerrainType::PLAIN;
            } else {
                return TerrainType::PLAIN;
            }
        } else if (height < config.hillHeight) {
            // 丘陵：根据湿度和温度决定
            if (moisture > 0.6f && temperature > 0.4f) {
                return TerrainType::FOREST;
            } else {
                return TerrainType::HILL;
            }
        } else if (height < config.mountainHeight) {
            // 山地
            if (temperature < 0.2f) {
                return TerrainType::SNOW_MOUNTAIN;
            } else {
                return TerrainType::MOUNTAIN;
            }
        } else {
            // 高山
            if (temperature < 0.1f) {
                return TerrainType::SNOW_MOUNTAIN;
            } else {
                return TerrainType::MOUNTAIN;
            }
        }
    }

    void normalizeHeightmap(HeightMap& heightmap) {
        if (heightmap.empty()) return;
        
        float minVal = *std::min_element(heightmap.begin(), heightmap.end());
        float maxVal = *std::max_element(heightmap.begin(), heightmap.end());
        float range = maxVal - minVal;
        
        if (range > 0.0f) {
            for (auto& val : heightmap) {
                val = (val - minVal) / range;
            }
        }
    }
    
    // 优化河流生成
    void generateRiverNetwork(TileMap& terrainMap, const HeightMap& heightmap,
                              const MapConfig& config, const RiverParams& params) {

        // 并行寻找河流源点 - 修复版本
        std::vector<std::pair<uint32_t, uint32_t>> riverSources;
        std::mutex sourcesMutex;

        // 使用更合理的分块大小
        const uint32_t chunkSize = 32;

        // 第一阶段：并行寻找源点
        auto findSources = [&](uint32_t startX, uint32_t startY, uint32_t endX, uint32_t endY) {
            std::vector<std::pair<uint32_t, uint32_t>> localSources;

            // 确保边界安全
            startX = std::max(startX, 1u);
            startY = std::max(startY, 1u);
            endX = std::min(endX, config.width - 1);
            endY = std::min(endY, config.height - 1);

            for (uint32_t y = startY; y < endY; ++y) {
                for (uint32_t x = startX; x < endX; ++x) {
                    uint32_t idx = y * config.width + x;
                    float height = heightmap[idx];

                    if (height >= params.minSourceHeight && height <= params.maxSourceHeight) {
                        // 检查是否是局部高点
                        bool isPeak = true;
                        for (int dy = -1; dy <= 1; dy++) {
                            for (int dx = -1; dx <= 1; dx++) {
                                if (dx == 0 && dy == 0) continue;

                                uint32_t nIdx = (y + dy) * config.width + (x + dx);
                                if (heightmap[nIdx] > height) {
                                    isPeak = false;
                                    break;
                                }
                            }
                            if (!isPeak) break;
                        }

                        if (isPeak) {
                            localSources.emplace_back(x, y);
                        }
                    }
                }
            }

            // 合并结果
            if (!localSources.empty()) {
                std::lock_guard<std::mutex> lock(sourcesMutex);
                riverSources.insert(riverSources.end(),
                                    localSources.begin(), localSources.end());
            }
        };

        // 并行查找源点
        m_parallelProcessor->parallelFor2DChunked(config.width, config.height, chunkSize, findSources);

        // 限制河流数量
        if (riverSources.size() > params.count) {
            std::shuffle(riverSources.begin(), riverSources.end(), m_rng);
            riverSources.resize(params.count);
        }

        // 如果没有找到源点，直接返回
        if (riverSources.empty()) {
            return;
        }

        // 第二阶段：并行生成每条河流
        // 使用1D并行处理每条河流
        const uint32_t riverCount = static_cast<uint32_t>(riverSources.size());

        // 创建河流缓冲区，避免直接修改terrainMap
        std::vector<std::vector<uint32_t>> riverBuffers(config.threadCount);
        for (auto& buffer : riverBuffers) {
            buffer.resize(terrainMap.size(), std::numeric_limits<uint32_t>::max());
        }

        std::atomic<uint32_t> nextRiver{0};
        std::mutex riverStatsMutex;

        auto generateRiver = [&](uint32_t threadId) {
            std::vector<uint32_t>& buffer = riverBuffers[threadId];
            std::mt19937 localRng(m_seed + threadId);

            while (true) {
                uint32_t riverIdx = nextRiver.fetch_add(1);
                if (riverIdx >= riverCount) break;

                auto [startX, startY] = riverSources[riverIdx];

                // 生成单条河流到本地缓冲区
                generateSingleRiverToBuffer(buffer, heightmap, config,
                                            startX, startY, params, localRng);
            }
        };

        // 启动工作线程
        std::vector<std::thread> riverWorkers;
        for (uint32_t t = 0; t < config.threadCount; ++t) {
            riverWorkers.emplace_back(generateRiver, t);
        }

        // 主线程也参与工作
        generateRiver(0);

        // 等待所有河流生成完成
        for (auto& worker : riverWorkers) {
            worker.join();
        }

        // 第三阶段：合并河流缓冲区到地形图
        mergeRiverBuffers(terrainMap, riverBuffers, config);
    }

    // 生成单条河流到本地缓冲区（避免竞争）
    void generateSingleRiverToBuffer(std::vector<uint32_t>& buffer,
                                     const HeightMap& heightmap,
                                     const MapConfig& config,
                                     uint32_t startX, uint32_t startY,
                                     const RiverParams& params,
                                     std::mt19937& rng) {

        // 河流点栈
        struct RiverPoint {
            uint32_t x, y;
            float height;
            bool isTributary;
            uint32_t depth;
        };

        std::stack<RiverPoint> riverStack;
        riverStack.push({startX, startY, heightmap[startY * config.width + startX], false, 0});

        std::uniform_real_distribution<float> stopDist(0.0f, 1.0f);

        while (!riverStack.empty()) {
            RiverPoint current = riverStack.top();
            riverStack.pop();

            uint32_t x = current.x;
            uint32_t y = current.y;

            // 检查边界
            if (x == 0 || x >= config.width - 1 || y == 0 || y >= config.height - 1) {
                continue;
            }

            uint32_t idx = y * config.width + x;

            // 标记为河流（在缓冲区中）
            float currentHeight = heightmap[idx];
            buffer[idx] = static_cast<uint32_t>(TerrainType::RIVER);

            // 检查是否到达海洋或已存在的河流
            if (currentHeight < config.seaLevel) {
                continue; // 流入海洋，停止
            }

            // 随机终止
            if (stopDist(rng) < 0.01f) {
                continue;
            }

            // 检查河流长度限制（通过深度）
            if (current.depth > params.maxRiverLength) {
                continue;
            }

            // 找到最低的邻居（水流方向）
            float minHeight = currentHeight;
            int bestDx = 0, bestDy = 0;

            // 检查8个方向
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;

                    uint32_t nx = x + dx;
                    uint32_t ny = y + dy;

                    if (nx < config.width && ny < config.height) {
                        uint32_t nIdx = ny * config.width + nx;
                        float nHeight = heightmap[nIdx];

                        if (nHeight < minHeight) {
                            minHeight = nHeight;
                            bestDx = dx;
                            bestDy = dy;
                        }
                    }
                }
            }

            // 如果没有找到下坡，河流结束
            if (bestDx == 0 && bestDy == 0) {
                continue;
            }

            // 继续生成下一个点
            uint32_t nextX = x + bestDx;
            uint32_t nextY = y + bestDy;

            riverStack.push({
                nextX,
                nextY,
                heightmap[nextY * config.width + nextX],
                current.isTributary,
                current.depth + 1
            });

            // 检查是否需要生成支流
            if (params.tributaries && !current.isTributary &&
                current.depth > 20 && current.depth % 30 == 0) {

                // 生成支流起点（偏移一些距离）
                float angle = std::uniform_real_distribution<float>(0.0f, 2.0f * M_PI)(rng);
                float distance = std::uniform_real_distribution<float>(3.0f, 8.0f)(rng);

                uint32_t tribX = static_cast<uint32_t>(x + std::cos(angle) * distance);
                uint32_t tribY = static_cast<uint32_t>(y + std::sin(angle) * distance);

                // 确保在边界内
                tribX = std::clamp(tribX, 1u, config.width - 2);
                tribY = std::clamp(tribY, 1u, config.height - 2);

                // 如果起点高度合适，生成支流
                uint32_t tribIdx = tribY * config.width + tribX;
                float tribHeight = heightmap[tribIdx];

                if (tribHeight >= params.minSourceHeight &&
                    tribHeight <= params.maxSourceHeight) {

                    riverStack.push({
                        tribX,
                        tribY,
                        tribHeight,
                        true,  // 标记为支流
                        current.depth + 1
                    });
                }
            }
        }
    }

    // 合并河流缓冲区到地形图
    void mergeRiverBuffers(TileMap& terrainMap,
                           const std::vector<std::vector<uint32_t>>& riverBuffers,
                           const MapConfig& config) {

        // 并行合并缓冲区
        m_parallelProcessor->parallelFor2D(config.width, config.height,
                                           [&](uint32_t x, uint32_t y) {
                                               uint32_t idx = y * config.width + x;

                                               // 检查所有缓冲区
                                               for (const auto& buffer : riverBuffers) {
                                                   if (buffer[idx] == static_cast<uint32_t>(TerrainType::RIVER)) {
                                                       // 标记为河流，但避免覆盖海洋
                                                       TerrainType current = static_cast<TerrainType>(terrainMap[idx]);
                                                       if (current != TerrainType::DEEP_OCEAN &&
                                                           current != TerrainType::SHALLOW_OCEAN &&
                                                           current != TerrainType::COAST) {
                                                           terrainMap[idx] = static_cast<uint32_t>(TerrainType::RIVER);
                                                       }
                                                       break; // 找到一个河流点即可
                                                   }
                                               }
                                           });
    }

    // 线程安全的单条河流生成
    void generateSingleRiverParallel(TileMap& terrainMap, const HeightMap& heightmap,
                                    const MapConfig& config, uint32_t startX, uint32_t startY,
                                    const RiverParams& params, std::mutex& terrainMutex) {
        // 实现单条河流生成的线程安全版本
        // 每个河流在本地缓冲区中生成，最后合并到主地形图
        
        std::vector<std::pair<uint32_t, uint32_t>> riverPoints;
        uint32_t x = startX;
        uint32_t y = startY;
        
        // 使用本地RNG避免线程竞争
        std::mt19937 localRng(m_seed + startX * 1000 + startY);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        
        while (true) {
            // 边界检查
            if (x == 0 || x >= config.width - 1 || y == 0 || y >= config.height - 1) {
                break;
            }
            
            riverPoints.emplace_back(x, y);
            
            uint32_t idx = y * config.width + x;
            TerrainType currentTerrain = static_cast<TerrainType>(terrainMap[idx]);
            
            // 检查是否到达海洋
            if (currentTerrain == TerrainType::DEEP_OCEAN ||
                currentTerrain == TerrainType::SHALLOW_OCEAN) {
                break;
            }
            
            // 随机终止
            if (dist(localRng) < 0.01f) {
                break;
            }
            
            // 河流长度限制
            if (riverPoints.size() > params.maxRiverLength) {
                break;
            }
            
            // 找到最低的邻居
            float minHeight = heightmap[idx];
            int bestDx = 0, bestDy = 0;
            
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    
                    uint32_t nx = x + dx;
                    uint32_t ny = y + dy;
                    
                    if (nx < config.width && ny < config.height) {
                        uint32_t nIdx = ny * config.width + nx;
                        float nHeight = heightmap[nIdx];
                        
                        if (nHeight < minHeight) {
                            minHeight = nHeight;
                            bestDx = dx;
                            bestDy = dy;
                        }
                    }
                }
            }
            
            if (bestDx == 0 && bestDy == 0) {
                break;
            }
            
            x += bestDx;
            y += bestDy;
        }
        
        // 合并到主地形图（需要加锁）
        if (!riverPoints.empty()) {
            std::lock_guard<std::mutex> lock(terrainMutex);
            for (const auto& [rx, ry] : riverPoints) {
                uint32_t idx = ry * config.width + rx;
                TerrainType currentTerrain = static_cast<TerrainType>(terrainMap[idx]);
                
                if (currentTerrain != TerrainType::DEEP_OCEAN &&
                    currentTerrain != TerrainType::SHALLOW_OCEAN) {
                    terrainMap[idx] = static_cast<uint32_t>(TerrainType::RIVER);
                }
            }
        }
    }

    void generateLakesParallel(TileMap& terrainMap, const HeightMap& heightmap,
                               const MapConfig& config, const RiverParams& params) {

        // 并行寻找低洼区域
        std::vector<std::pair<uint32_t, uint32_t>> depressionPoints;
        std::mutex depressionMutex;

        // 为每个线程创建独立的随机数生成器
        std::vector<std::mt19937> threadRngs(config.threadCount);
        for (uint32_t i = 0; i < config.threadCount; ++i) {
            threadRngs[i] = std::mt19937(m_seed + 321 + i);
        }

        // 并行寻找低洼区域
        m_parallelProcessor->parallelFor2DChunked(config.width, config.height, 32,
                                                  [&](uint32_t startX, uint32_t startY, uint32_t endX, uint32_t endY) {
                                                      // 获取当前线程的随机数生成器
                                                      static thread_local uint32_t threadId = []() {
                                                          static std::atomic<uint32_t> counter{0};
                                                          return counter.fetch_add(1);
                                                      }();

                                                      std::mt19937& lakeRng = threadRngs[threadId % config.threadCount];
                                                      std::uniform_real_distribution<float> lakeDist(0.0f, 1.0f);

                                                      std::vector<std::pair<uint32_t, uint32_t>> localDepressions;

                                                      // 确保边界安全
                                                      startX = std::max(startX, 2u);
                                                      startY = std::max(startY, 2u);
                                                      endX = std::min(endX, config.width - 2);
                                                      endY = std::min(endY, config.height - 2);

                                                      for (uint32_t y = startY; y < endY; ++y) {
                                                          for (uint32_t x = startX; x < endX; ++x) {
                                                              uint32_t idx = y * config.width + x;
                                                              float height = heightmap[idx];

                                                              // 检查是否是局部低点
                                                              bool isDepression = true;

                                                              // 优化：使用展开循环
                                                              for (int dy = -2; dy <= 2; dy++) {
                                                                  if (!isDepression) break;

                                                                  int currentY = static_cast<int>(y) + dy;
                                                                  if (currentY < 0 || currentY >= static_cast<int>(config.height)) {
                                                                      isDepression = false;
                                                                      break;
                                                                  }

                                                                  for (int dx = -2; dx <= 2; dx++) {
                                                                      if (dx == 0 && dy == 0) continue;

                                                                      int currentX = static_cast<int>(x) + dx;
                                                                      if (currentX < 0 || currentX >= static_cast<int>(config.width)) {
                                                                          isDepression = false;
                                                                          break;
                                                                      }

                                                                      uint32_t nIdx = currentY * config.width + currentX;
                                                                      if (heightmap[nIdx] < height) {
                                                                          isDepression = false;
                                                                          break;
                                                                      }
                                                                  }
                                                              }

                                                              if (isDepression && lakeDist(lakeRng) < params.lakeProbability) {
                                                                  localDepressions.emplace_back(x, y);
                                                              }
                                                          }
                                                      }

                                                      // 合并结果
                                                      if (!localDepressions.empty()) {
                                                          std::lock_guard<std::mutex> lock(depressionMutex);
                                                          depressionPoints.insert(depressionPoints.end(),
                                                                                  localDepressions.begin(), localDepressions.end());
                                                      }
                                                  });

        // 限制湖泊数量，避免过多
        const uint32_t maxLakes = std::min(static_cast<uint32_t>(depressionPoints.size()),
                                           static_cast<uint32_t>((config.width * config.height) * 0.0001f));

        if (maxLakes == 0) return;

        // 随机选择湖泊中心点
        std::shuffle(depressionPoints.begin(), depressionPoints.end(), m_rng);
        depressionPoints.resize(maxLakes);

        // 并行生成湖泊（使用任务队列）
        generateLakesParallelTasks(terrainMap, heightmap, config, params, depressionPoints);
    }

    // 使用任务队列并行生成湖泊
    void generateLakesParallelTasks(TileMap& terrainMap, const HeightMap& heightmap,
                                    const MapConfig& config, const RiverParams& params,
                                    const std::vector<std::pair<uint32_t, uint32_t>>& lakeCenters) {

        // 创建任务队列
        std::queue<uint32_t> taskQueue;
        for (uint32_t i = 0; i < lakeCenters.size(); ++i) {
            taskQueue.push(i);
        }

        std::mutex queueMutex;
        std::mutex terrainMutex;

        // 工作线程函数
        auto lakeWorker = [&]() {
            std::mt19937 localRng(m_seed + std::hash<std::thread::id>{}(std::this_thread::get_id()));

            while (true) {
                uint32_t taskIdx;

                // 获取任务
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    if (taskQueue.empty()) break;
                    taskIdx = taskQueue.front();
                    taskQueue.pop();
                }

                auto [centerX, centerY] = lakeCenters[taskIdx];

                // 生成湖泊到本地缓冲区
                std::vector<uint32_t> lakeBuffer(terrainMap.size(), std::numeric_limits<uint32_t>::max());
                generateLakeToBuffer(lakeBuffer, heightmap, config, centerX, centerY, params, localRng);

                // 合并到主地形图
                {
                    std::lock_guard<std::mutex> lock(terrainMutex);
                    mergeLakeBuffer(terrainMap, lakeBuffer, config);
                }
            }
        };

        // 启动工作线程
        uint32_t numWorkers = std::min(config.threadCount, static_cast<uint32_t>(lakeCenters.size()));
        std::vector<std::thread> workers;

        for (uint32_t i = 0; i < numWorkers; ++i) {
            workers.emplace_back(lakeWorker);
        }

        // 主线程也参与工作
        lakeWorker();

        // 等待所有工作线程完成
        for (auto& worker : workers) {
            worker.join();
        }
    }

    // 生成湖泊到缓冲区
    void generateLakeToBuffer(std::vector<uint32_t>& buffer, const HeightMap& heightmap,
                              const MapConfig& config, uint32_t centerX, uint32_t centerY,
                              const RiverParams& params, std::mt19937& rng) {

        std::uniform_real_distribution<float> sizeDist(params.minLakeSize, params.maxLakeSize);
        std::uniform_real_distribution<float> noiseDist(0.0f, 1.0f);

        float baseSize = sizeDist(rng);

        // 生成随机参数用于形状变化
        float irregularity = 0.3f + noiseDist(rng) * 0.4f;
        float distortion = 0.2f + noiseDist(rng) * 0.3f;
        int lobes = 5 + static_cast<int>(noiseDist(rng) * 5);

        // 湖泊类型
        enum LakeType { CIRCULAR, ELLIPTICAL, IRREGULAR };
        LakeType lakeType = static_cast<LakeType>(static_cast<int>(noiseDist(rng) * 3));

        // 预计算一些值
        float maxRadius = baseSize * 1.5f;
        int maxRadiusInt = static_cast<int>(maxRadius);

        // 计算湖泊边界
        int startX = static_cast<int>(centerX) - maxRadiusInt;
        int startY = static_cast<int>(centerY) - maxRadiusInt;
        int endX = static_cast<int>(centerX) + maxRadiusInt;
        int endY = static_cast<int>(centerY) + maxRadiusInt;

        // 限制边界
        startX = std::max(startX, 0);
        startY = std::max(startY, 0);
        endX = std::min(endX, static_cast<int>(config.width) - 1);
        endY = std::min(endY, static_cast<int>(config.height) - 1);

        // 处理湖泊区域
        for (int y = startY; y <= endY; ++y) {
            for (int x = startX; x <= endX; ++x) {
                int dx = x - static_cast<int>(centerX);
                int dy = y - static_cast<int>(centerY);

                float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (dist > maxRadius) continue;

                // 基本形状：圆形或椭圆形
                float normalizedDist;
                if (lakeType == ELLIPTICAL) {
                    float rx = baseSize * (0.8f + noiseDist(rng) * 0.4f);
                    float ry = baseSize * (0.8f + noiseDist(rng) * 0.4f);
                    normalizedDist = std::sqrt((dx * dx) / (rx * rx) + (dy * dy) / (ry * ry));
                } else {
                    normalizedDist = dist / baseSize;
                }

                // 应用不规则性
                float angle = std::atan2(dy, dx);
                float noiseValue = 0.0f;

                switch (lakeType) {
                case CIRCULAR:
                    noiseValue = (std::sin(angle * lobes) * 0.1f + 1.0f) * irregularity;
                    break;
                case ELLIPTICAL:
                    noiseValue = (std::sin(angle * 8 + dist * 0.2f) * 0.15f + 1.0f) * irregularity;
                    break;
                case IRREGULAR:
                    noiseValue = (std::sin(angle * lobes) * 0.2f +
                                  std::sin(angle * lobes * 2 + dist * 0.3f) * 0.15f +
                                  std::sin(dist * 0.5f) * 0.1f + 1.0f) * irregularity;
                    break;
                }

                // 应用柏林噪声增加细节
                float nx = x / 10.0f;
                float ny = y / 10.0f;
                float perlinNoise = m_noiseGen->applyPerlinNoise(nx, ny) * 0.5f + 0.5f;
                noiseValue *= (0.7f + perlinNoise * 0.3f);

                // 添加随机扰动
                std::uniform_real_distribution<float> localDist(0.0f, 1.0f);
                float randomDistortion = 1.0f + (localDist(rng) - 0.5f) * distortion * 2.0f;

                // 最终距离计算
                float finalThreshold = 1.0f * noiseValue * randomDistortion;

                // 使用平滑过渡
                float alpha = 1.0f - smoothstep(finalThreshold - 0.3f, finalThreshold + 0.3f, normalizedDist);

                // 如果这个位置在湖泊内
                if (alpha > 0.5f) {
                    uint32_t idx = y * config.width + x;

                    // 根据alpha值决定是湖泊还是浅滩
                    if (alpha > 0.8f) {
                        buffer[idx] = static_cast<uint32_t>(TerrainType::LAKE);
                    } else {
                        // 边缘区域可能是浅滩
                        if (localDist(rng) < 0.3f) {
                            buffer[idx] = static_cast<uint32_t>(TerrainType::BEACH);
                        } else {
                            buffer[idx] = static_cast<uint32_t>(TerrainType::LAKE);
                        }
                    }

                    // 随机添加小岛
                    if (alpha < 0.95f && localDist(rng) < 0.02f) {
                        buffer[idx] = static_cast<uint32_t>(TerrainType::PLAIN);
                    }
                }
            }
        }

        // 平滑湖泊边界（在缓冲区中进行）
        smoothLakeBoundaryInBuffer(buffer, config, centerX, centerY, baseSize);
    }

    // 合并湖泊缓冲区到地形图
    void mergeLakeBuffer(TileMap& terrainMap, const std::vector<uint32_t>& lakeBuffer,
                         const MapConfig& config) {

        for (uint32_t i = 0; i < terrainMap.size(); ++i) {
            if (lakeBuffer[i] != std::numeric_limits<uint32_t>::max()) {
                TerrainType current = static_cast<TerrainType>(terrainMap[i]);

                // 只覆盖陆地，并且不是河流
                if (current != TerrainType::DEEP_OCEAN &&
                    current != TerrainType::SHALLOW_OCEAN &&
                    current != TerrainType::COAST &&
                    current != TerrainType::RIVER) {

                    terrainMap[i] = lakeBuffer[i];
                }
            }
        }
    }

    float smoothstep(float edge0, float edge1, float x) {
        x = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    // 在缓冲区中平滑湖泊边界
    void smoothLakeBoundaryInBuffer(std::vector<uint32_t>& buffer, const MapConfig& config,
                                    uint32_t centerX, uint32_t centerY, float lakeSize) {

        std::vector<uint32_t> tempBuffer = buffer;
        int radius = static_cast<int>(lakeSize) + 2;

        // 计算边界
        int startX = std::max(static_cast<int>(centerX) - radius, 0);
        int startY = std::max(static_cast<int>(centerY) - radius, 0);
        int endX = std::min(static_cast<int>(centerX) + radius, static_cast<int>(config.width) - 1);
        int endY = std::min(static_cast<int>(centerY) + radius, static_cast<int>(config.height) - 1);

        for (int y = startY; y <= endY; ++y) {
            for (int x = startX; x <= endX; ++x) {
                uint32_t idx = y * config.width + x;

                if (buffer[idx] == static_cast<uint32_t>(TerrainType::LAKE)) {
                    // 检查周围8个邻居
                    int lakeNeighbors = 0;
                    int totalNeighbors = 0;

                    for (int ndy = -1; ndy <= 1; ndy++) {
                        for (int ndx = -1; ndx <= 1; ndx++) {
                            if (ndx == 0 && ndy == 0) continue;

                            int nx = x + ndx;
                            int ny = y + ndy;

                            if (nx >= 0 && nx < static_cast<int>(config.width) &&
                                ny >= 0 && ny < static_cast<int>(config.height)) {
                                totalNeighbors++;
                                uint32_t nIdx = ny * config.width + nx;

                                if (buffer[nIdx] == static_cast<uint32_t>(TerrainType::LAKE)) {
                                    lakeNeighbors++;
                                }
                            }
                        }
                    }

                    // 如果湖泊单元格被陆地包围太多，可能是孤岛
                    if (lakeNeighbors < 3 && totalNeighbors > 0) {
                        float lakeRatio = static_cast<float>(lakeNeighbors) / totalNeighbors;
                        if (lakeRatio < 0.4f) {
                            tempBuffer[idx] = static_cast<uint32_t>(TerrainType::PLAIN);
                        }
                    }
                }
            }
        }

        buffer = tempBuffer;
    }
    
    // 优化统计计算
    void calculateStatistics(MapData& data) {
        auto& stats = data.stats;
        
        // 为每个线程创建本地统计
        uint32_t threadCount = m_parallelProcessor->getThreadCount();
        std::vector<MapData::Statistics> localStats(threadCount);
        
        // 初始化本地统计
        for (auto& local : localStats) {
            local.minHeight = std::numeric_limits<float>::max();
            local.maxHeight = std::numeric_limits<float>::lowest();
        }
        
        // 并行计算统计
        m_parallelProcessor->parallelFor2DChunked(data.config.width, data.config.height, 64,
            [&](uint32_t startX, uint32_t startY, uint32_t endX, uint32_t endY) {
                uint32_t threadId = startY / (data.config.height / threadCount);
                auto& local = localStats[threadId];
                
                float localTotalHeight = 0.0f;
                uint32_t localCount = 0;
                
                for (uint32_t y = startY; y < endY; ++y) {
                    for (uint32_t x = startX; x < endX; ++x) {
                        uint32_t idx = y * data.config.width + x;
                        float height = data.heightMap[idx];
                        
                        localTotalHeight += height;
                        local.minHeight = std::min(local.minHeight, height);
                        local.maxHeight = std::max(local.maxHeight, height);
                        localCount++;
                        
                        TerrainType terrain = static_cast<TerrainType>(data.terrainMap[idx]);
                        
                        switch (terrain) {
                            case TerrainType::DEEP_OCEAN:
                            case TerrainType::SHALLOW_OCEAN:
                            case TerrainType::COAST:
                            case TerrainType::LAKE:
                            case TerrainType::RIVER:
                                local.waterTiles++;
                                if (terrain == TerrainType::RIVER) {
                                    local.riverTiles++;
                                }
                                break;
                            default:
                                local.landTiles++;
                                break;
                        }
                        
                        if (terrain == TerrainType::FOREST) {
                            local.forestTiles++;
                        } else if (terrain == TerrainType::MOUNTAIN ||
                                  terrain == TerrainType::SNOW_MOUNTAIN) {
                            local.mountainTiles++;
                        }
                    }
                }
                
                // 计算本地平均高度
                if (localCount > 0) {
                    local.averageHeight = localTotalHeight / localCount;
                }
            });
        
        // 合并统计结果
        stats = MapData::Statistics();
        float totalWeightedHeight = 0.0f;
        uint32_t totalCount = 0;
        
        for (const auto& local : localStats) {
            stats.waterTiles += local.waterTiles;
            stats.landTiles += local.landTiles;
            stats.forestTiles += local.forestTiles;
            stats.mountainTiles += local.mountainTiles;
            stats.riverTiles += local.riverTiles;
            
            stats.minHeight = std::min(stats.minHeight, local.minHeight);
            stats.maxHeight = std::max(stats.maxHeight, local.maxHeight);
            
            uint32_t localCount = local.waterTiles + local.landTiles;
            totalWeightedHeight += local.averageHeight * localCount;
            totalCount += localCount;
        }
        
        if (totalCount > 0) {
            stats.averageHeight = totalWeightedHeight / totalCount;
        } else {
            stats.averageHeight = 0.0f;
        }
    }
};

// MapGeneratorInternal公共接口实现
MapGeneratorInternal::MapGeneratorInternal(uint32_t seed)
    : m_impl(std::make_unique<Impl>(seed)) {
}

MapGeneratorInternal::~MapGeneratorInternal() = default;

std::shared_ptr<MapData> MapGeneratorInternal::generate(const MapConfig& config) {
    return m_impl->generate(config);
}

std::vector<std::shared_ptr<MapData>> 
MapGeneratorInternal::generateBatch(const MapConfig& baseConfig, uint32_t count) {
    return m_impl->generateBatch(baseConfig, count);
}

HeightMap MapGeneratorInternal::generateHeightmapOnly(const MapConfig& config) {
    return m_impl->generateHeightmapOnly(config);
}

TileMap MapGeneratorInternal::generateTerrainOnly(const HeightMap& heightmap, 
                                                 const MapConfig& config) {
    return m_impl->generateTerrainOnly(heightmap, config);
}

void MapGeneratorInternal::applyErosion(HeightMap& heightmap, const MapConfig& config,
                                       const ErosionParams& params) {
    m_impl->applyErosion(heightmap, config, params);
}

void MapGeneratorInternal::generateRivers(TileMap& terrainMap, const HeightMap& heightmap,
                                         const MapConfig& config, const RiverParams& params) {
    m_impl->generateRivers(terrainMap, heightmap, config, params);
}

} // namespace internal
} // namespace MapGenerator

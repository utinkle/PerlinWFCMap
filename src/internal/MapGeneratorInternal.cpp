#define _USE_MATH_DEFINES
#include "MapGeneratorInternal.h"
#include "NoiseGenerator.h"
#include "WFCGenerator.h"
#include "ThreadPool.h"
#include <algorithm>
#include <chrono>
#include <memory>
#include <random>
#include <future>
#include <iostream>
#define _USE_MATH_DEFINES
#include <cmath>

namespace MapGenerator {
namespace internal {

class MapGeneratorInternal::Impl {
private:
    std::mt19937 m_rng;
    uint32_t m_seed;
    std::unique_ptr<NoiseGenerator> m_noiseGen;
    std::unique_ptr<WFCGenerator> m_wfcGen;
    std::unique_ptr<ThreadPool> m_threadPool;
    
    // 缓存
    std::unordered_map<uint64_t, std::shared_ptr<MapData>> m_cache;
    
public:
    Impl(uint32_t seed) 
        : m_seed(seed), m_rng(seed),
          m_noiseGen(std::make_unique<NoiseGenerator>(seed)),
          m_wfcGen(std::make_unique<WFCGenerator>(seed)),
          m_threadPool(std::make_unique<ThreadPool>(std::thread::hardware_concurrency())) {
    }
    
    std::shared_ptr<MapData> generate(const MapConfig& config) {
        // 检查缓存
        uint64_t cacheKey = computeCacheKey(config);
        auto it = m_cache.find(cacheKey);
        if (it != m_cache.end()) {
            return it->second;
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
                
        // 步骤6: 生成装饰图
        data->decorationMap = generateDecorationOnly(data->heightMap, data->terrainMap, config);
        
        // 步骤7: 生成资源图
        WFCParams wfcParams = createWFCParamsFromConfig(config);
        data->resourceMap = m_wfcGen->generateResourceMap(
            data->terrainMap, data->decorationMap, 
            config.width, config.height, wfcParams);
        
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
        
        return m_noiseGen->generateHeightMap(config.width, config.height, noiseParams);
    }
    
    TileMap generateTerrainOnly(const HeightMap& heightmap, const MapConfig& config) {
        TileMap terrainMap(heightmap.size());
        
        // 并行处理地形生成
        std::vector<std::future<void>> futures;
        uint32_t chunkSize = config.height / m_threadPool->getThreadCount();
        
        for (uint32_t chunk = 0; chunk < m_threadPool->getThreadCount(); chunk++) {
            uint32_t startY = chunk * chunkSize;
            uint32_t endY = (chunk == m_threadPool->getThreadCount() - 1) ? 
                           config.height : startY + chunkSize;
            
            futures.push_back(m_threadPool->enqueueTask([&, startY, endY]() {
                generateTerrainChunk(heightmap, terrainMap, config, startY, endY);
            }));
        }
        
        for (auto& future : futures) {
            future.get();
        }
        
        return terrainMap;
    }
    
    TileMap generateDecorationOnly(const HeightMap& heightmap,
                                  const TileMap& terrainMap,
                                  const MapConfig& config) {
        WFCParams wfcParams = createWFCParamsFromConfig(config);
        
        // 生成基础装饰
        TileMap decorationMap = m_wfcGen->generateDecorationMap(
            heightmap, terrainMap, config.width, config.height, wfcParams);
        
        // 应用装饰参数
        DecorationParams decParams = createDecorationParamsFromConfig(config);
        addDecorations(decorationMap, terrainMap, heightmap, config, decParams);
        
        return decorationMap;
    }
    
    void applyErosion(HeightMap& heightmap, const MapConfig& config,
                     const ErosionParams& params) {
        m_noiseGen->applyErosion(heightmap, config.width, config.height, params);
        
        // 重新归一化高度图
        normalizeHeightmap(heightmap);
    }
    
    void generateRivers(TileMap& terrainMap, const HeightMap& heightmap,
                       const MapConfig& config, const RiverParams& params) {
        // 生成河流网络
        generateRiverNetwork(terrainMap, heightmap, config, params);
        
        // 生成湖泊
        if (params.generateLakes) {
            generateLakes(terrainMap, heightmap, config, params);
        }
    }
    
    void addDecorations(TileMap& decorationMap, const TileMap& terrainMap,
                       const HeightMap& heightmap, const MapConfig& config,
                       const DecorationParams& params) {
        // 基于参数添加装饰
        addTreeDecorations(decorationMap, terrainMap, heightmap, config, params);
        addRockDecorations(decorationMap, terrainMap, heightmap, config, params);
        addVegetationDecorations(decorationMap, terrainMap, heightmap, config, params);
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
        }
        
        return params;
    }
    
    WFCParams createWFCParamsFromConfig(const MapConfig& config) {
        WFCParams params;
        params.iterations = config.wfcIterations;
        params.entropyWeight = config.wfcEntropyWeight;
        params.enableBacktracking = config.wfcEnableBacktracking;
        params.temperature = 1.0f;
        params.useWeights = true;
        
        // 根据地图大小调整参数
        if (config.width * config.height > 1000000) { // 大型地图
            params.iterations = std::min(params.iterations, 500u);
            params.patternSize = 2;
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
                float temperature = calculateTemperature(x, y, config, biomeParams);
                float moisture = calculateMoisture(x, y, config, height);
                
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
        }
        
        return params;
    }
    
    float calculateTemperature(uint32_t x, uint32_t y, const MapConfig& config,
                              const BiomeParams& params) {
        // 纬度影响（y坐标）
        float latitude = static_cast<float>(y) / config.height;
        float baseTemp = config.temperature * (1.0f - fabs(latitude - 0.5f) * 2.0f);
        
        // 高度影响
        float heightTemp = 0.0f; // 将在调用处计算
        
        // 噪声影响
        float noiseX = x / params.temperatureScale;
        float noiseY = y / params.temperatureScale;
        std::mt19937 tempRng(m_seed + 123);
        std::uniform_real_distribution<float> tempNoise(-0.1f, 0.1f);
        
        float tempVariation = tempNoise(tempRng) * 0.1f;
        
        return baseTemp + params.temperatureBias + tempVariation;
    }
    
    float calculateMoisture(uint32_t x, uint32_t y, const MapConfig& config, float height) {
        // 基础湿度
        float baseMoisture = config.humidity;
        
        // 高度影响（高处较干）
        float heightEffect = (1.0f - height) * 0.5f;
        
        // 噪声影响
        float noiseX = x / 50.0f;
        float noiseY = y / 50.0f;
        std::mt19937 moistureRng(m_seed + 456);
        std::uniform_real_distribution<float> moistureNoise(-0.15f, 0.15f);
        
        float moistureVariation = moistureNoise(moistureRng) * 0.2f;
        
        return std::clamp(baseMoisture + heightEffect + moistureVariation, 0.0f, 1.0f);
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
            if (temperature < 0.3f) {
                return TerrainType::SNOW_MOUNTAIN;
            } else {
                return TerrainType::MOUNTAIN;
            }
        } else {
            // 高山
            return TerrainType::SNOW_MOUNTAIN;
        }
    }
    
    void applyAdvancedTerrainFeatures(HeightMap& heightmap, TileMap& terrainMap,
                                     const MapConfig& config) {

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
    
    void generateRiverNetwork(TileMap& terrainMap, const HeightMap& heightmap,
                             const MapConfig& config, const RiverParams& params) {
        std::vector<std::pair<uint32_t, uint32_t>> riverSources;
        
        // 寻找河流源点（高处）
        for (uint32_t y = 1; y < config.height - 1; y++) {
            for (uint32_t x = 1; x < config.width - 1; x++) {
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
                    
                    if (isPeak && riverSources.size() < params.count) {
                        riverSources.emplace_back(x, y);
                    }
                }
            }
        }
        
        // 生成每条河流
        for (const auto& source : riverSources) {
            generateSingleRiver(terrainMap, heightmap, config, source.first, source.second, params);
        }
    }
    
    void generateSingleRiver(TileMap& terrainMap, const HeightMap& heightmap,
                           const MapConfig& config, uint32_t startX, uint32_t startY,
                           const RiverParams& params) {
        uint32_t x = startX;
        uint32_t y = startY;
        std::vector<std::pair<uint32_t, uint32_t>> riverPath;
        
        while (x > 0 && x < config.width - 1 && y > 0 && y < config.height - 1) {
            riverPath.emplace_back(x, y);
            
            // 标记为河流
            uint32_t idx = y * config.width + x;
            TerrainType currentTerrain = static_cast<TerrainType>(terrainMap[idx]);
            
            if (currentTerrain != TerrainType::DEEP_OCEAN &&
                currentTerrain != TerrainType::SHALLOW_OCEAN) {
                terrainMap[idx] = static_cast<uint32_t>(TerrainType::RIVER);
            }
            
            // 找到最低的邻居（水流方向）
            float minHeight = heightmap[idx];
            int bestDx = 0, bestDy = 0;
            
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    
                    uint32_t nx = x + dx;
                    uint32_t ny = y + dy;
                    uint32_t nIdx = ny * config.width + nx;
                    
                    if (heightmap[nIdx] < minHeight) {
                        minHeight = heightmap[nIdx];
                        bestDx = dx;
                        bestDy = dy;
                    }
                }
            }
            
            // 如果找不到更低点，河流结束
            if (bestDx == 0 && bestDy == 0) {
                break;
            }
            
            // 移动到下一个点
            x += bestDx;
            y += bestDy;
            
            // 如果流入海洋，结束
            TerrainType nextTerrain = static_cast<TerrainType>(terrainMap[y * config.width + x]);
            if (nextTerrain == TerrainType::DEEP_OCEAN ||
                nextTerrain == TerrainType::SHALLOW_OCEAN) {
                terrainMap[y * config.width + x] = static_cast<uint32_t>(TerrainType::RIVER);
                break;
            }
            
            // 随机终止（模拟蒸发）
            std::mt19937 riverRng(m_seed + x * 1000 + y);
            std::uniform_real_distribution<float> stopDist(0.0f, 1.0f);
            if (stopDist(riverRng) < 0.01f) { // 1%概率终止
                break;
            }
        }
        
        // 生成支流
        if (params.tributaries && riverPath.size() > 10) {
            generateTributaries(terrainMap, heightmap, config, riverPath, params);
        }
    }
    
    void generateTributaries(TileMap& terrainMap, const HeightMap& heightmap,
                           const MapConfig& config, 
                           const std::vector<std::pair<uint32_t, uint32_t>>& mainRiver,
                           const RiverParams& params) {
        std::mt19937 tribRng(m_seed + 789);
        std::uniform_real_distribution<float> angleDist(params.minTributaryAngle, 
                                                       params.maxTributaryAngle);
        std::uniform_int_distribution<uint32_t> pointDist(5, mainRiver.size() - 5);
        
        uint32_t numTributaries = std::min(3u, static_cast<uint32_t>(mainRiver.size() / 20));
        
        for (uint32_t i = 0; i < numTributaries; i++) {
            uint32_t startPoint = pointDist(tribRng);
            uint32_t startX = mainRiver[startPoint].first;
            uint32_t startY = mainRiver[startPoint].second;
            
            // 从主河道偏移开始支流
            float angle = angleDist(tribRng) * M_PI / 180.0f;
            uint32_t offsetX = static_cast<uint32_t>(cos(angle) * 5);
            uint32_t offsetY = static_cast<uint32_t>(sin(angle) * 5);
            
            uint32_t tribStartX = std::clamp(startX + offsetX, 1u, config.width - 2);
            uint32_t tribStartY = std::clamp(startY + offsetY, 1u, config.height - 2);
            
            // 生成支流
            generateSingleRiver(terrainMap, heightmap, config, tribStartX, tribStartY, params);
        }
    }
    
    void generateLakes(TileMap& terrainMap, const HeightMap& heightmap,
                      const MapConfig& config, const RiverParams& params) {
        std::mt19937 lakeRng(m_seed + 321);
        std::uniform_real_distribution<float> lakeDist(0.0f, 1.0f);
        
        // 寻找低洼区域
        for (uint32_t y = 2; y < config.height - 2; y++) {
            for (uint32_t x = 2; x < config.width - 2; x++) {
                uint32_t idx = y * config.width + x;
                float height = heightmap[idx];
                
                // 检查是否是局部低点
                bool isDepression = true;
                for (int dy = -2; dy <= 2; dy++) {
                    for (int dx = -2; dx <= 2; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        
                        uint32_t nIdx = (y + dy) * config.width + (x + dx);
                        if (heightmap[nIdx] < height) {
                            isDepression = false;
                            break;
                        }
                    }
                    if (!isDepression) break;
                }
                
                if (isDepression && lakeDist(lakeRng) < params.lakeProbability) {
                    // 生成湖泊
                    generateLake(terrainMap, heightmap, config, x, y, params);
                }
            }
        }
    }
    
    void generateLake(TileMap& terrainMap, const HeightMap& heightmap,
                     const MapConfig& config, uint32_t centerX, uint32_t centerY,
                     const RiverParams& params) {
        std::mt19937 lakeShapeRng(m_seed + centerX * 100 + centerY);
        std::uniform_real_distribution<float> sizeDist(params.minLakeSize, params.maxLakeSize);
        
        float lakeSize = sizeDist(lakeShapeRng);
        
        for (int dy = -static_cast<int>(lakeSize); dy <= static_cast<int>(lakeSize); dy++) {
            for (int dx = -static_cast<int>(lakeSize); dx <= static_cast<int>(lakeSize); dx++) {
                uint32_t x = centerX + dx;
                uint32_t y = centerY + dy;
                
                if (x < config.width && y < config.height) {
                    // 椭圆形状
                    float dist = sqrt(dx * dx + dy * dy);
                    if (dist <= lakeSize) {
                        uint32_t idx = y * config.width + x;
                        TerrainType current = static_cast<TerrainType>(terrainMap[idx]);
                        
                        // 只覆盖陆地
                        if (current != TerrainType::DEEP_OCEAN &&
                            current != TerrainType::SHALLOW_OCEAN &&
                            current != TerrainType::COAST &&
                            current != TerrainType::RIVER) {
                            terrainMap[idx] = static_cast<uint32_t>(TerrainType::LAKE);
                        }
                    }
                }
            }
        }
    }
    
    void addTreeDecorations(TileMap& decorationMap, const TileMap& terrainMap,
                           const HeightMap& heightmap, const MapConfig& config,
                           const DecorationParams& params) {
        std::mt19937 treeRng(m_seed + 111);
        
        for (uint32_t y = 0; y < config.height; y++) {
            for (uint32_t x = 0; x < config.width; x++) {
                uint32_t idx = y * config.width + x;
                TerrainType terrain = static_cast<TerrainType>(terrainMap[idx]);
                TerrainType currentDeco = static_cast<TerrainType>(decorationMap[idx]);
                
                // 只在适合的地形上添加树木
                if (terrain == TerrainType::FOREST && currentDeco == TerrainType::GRASS) {
                    float height = heightmap[idx];
                    
                    // 计算树木概率
                    float treeProb = params.treeDensity;
                    
                    // 高度影响
                    if (height > 0.7f) {
                        treeProb *= 0.5f; // 高处树木较少
                    }
                    
                    // 检查是否在集群中
                    if (isInTreeCluster(x, y, decorationMap, config, params)) {
                        treeProb *= 1.5f;
                    }
                    
                    std::uniform_real_distribution<float> treeDist(0.0f, 1.0f);
                    if (treeDist(treeRng) < treeProb) {
                        // 选择树木类型
                        TerrainType treeType;
                        if (height > 0.8f && config.temperature < 0.3f) {
                            treeType = TerrainType::TREE_SNOW;
                        } else if (config.climate == ClimateType::TROPICAL && 
                                  height < 0.5f) {
                            treeType = TerrainType::TREE_PALM;
                        } else if (treeDist(treeRng) < 0.3f) {
                            treeType = TerrainType::TREE_DENSE;
                        } else {
                            treeType = TerrainType::TREE_SPARSE;
                        }
                        
                        decorationMap[idx] = static_cast<uint32_t>(treeType);
                    }
                }
            }
        }
    }
    
    void addRockDecorations(TileMap& decorationMap, const TileMap& terrainMap,
                           const HeightMap& heightmap, const MapConfig& config,
                           const DecorationParams& params) {
        std::mt19937 rockRng(m_seed + 222);
        
        for (uint32_t y = 0; y < config.height; y++) {
            for (uint32_t x = 0; x < config.width; x++) {
                uint32_t idx = y * config.width + x;
                TerrainType terrain = static_cast<TerrainType>(terrainMap[idx]);
                TerrainType currentDeco = static_cast<TerrainType>(decorationMap[idx]);
                
                // 适合岩石的地形
                if ((terrain == TerrainType::MOUNTAIN || terrain == TerrainType::HILL || 
                     terrain == TerrainType::DESERT) && currentDeco == TerrainType::GRASS) {
                    
                    float height = heightmap[idx];
                    
                    // 计算岩石概率
                    float rockProb = params.rockDensity;
                    
                    // 坡度影响
                    float slope = calculateSlope(x, y, heightmap, config);
                    if (slope > 0.2f) {
                        rockProb *= params.rockOnSlopeBias;
                    }
                    
                    std::uniform_real_distribution<float> rockDist(0.0f, 1.0f);
                    if (rockDist(rockRng) < rockProb) {
                        // 选择岩石类型
                        TerrainType rockType;
                        if (height > 0.85f || terrain == TerrainType::MOUNTAIN) {
                            rockType = TerrainType::ROCK_LARGE;
                        } else {
                            rockType = TerrainType::ROCK_SMALL;
                        }
                        
                        decorationMap[idx] = static_cast<uint32_t>(rockType);
                    }
                }
            }
        }
    }
    
    void addVegetationDecorations(TileMap& decorationMap, const TileMap& terrainMap,
                                 const HeightMap& heightmap, const MapConfig& config,
                                 const DecorationParams& params) {
        std::mt19937 vegRng(m_seed + 333);
        
        for (uint32_t y = 0; y < config.height; y++) {
            for (uint32_t x = 0; x < config.width; x++) {
                uint32_t idx = y * config.width + x;
                TerrainType terrain = static_cast<TerrainType>(terrainMap[idx]);
                TerrainType currentDeco = static_cast<TerrainType>(decorationMap[idx]);
                
                // 只在草地上添加植被
                if (currentDeco == TerrainType::GRASS) {
                    float moisture = calculateMoisture(x, y, config, heightmap[idx]);
                    
                    // 灌木
                    std::uniform_real_distribution<float> bushDist(0.0f, 1.0f);
                    if (bushDist(vegRng) < params.bushDensity * moisture) {
                        decorationMap[idx] = static_cast<uint32_t>(TerrainType::BUSH);
                        continue;
                    }
                    
                    // 花朵
                    std::uniform_real_distribution<float> flowerDist(0.0f, 1.0f);
                    if (flowerDist(vegRng) < params.flowerDensity * moisture) {
                        decorationMap[idx] = static_cast<uint32_t>(TerrainType::FLOWERS);
                        continue;
                    }
                    
                    // 高草
                    std::uniform_real_distribution<float> grassDist(0.0f, 1.0f);
                    if (grassDist(vegRng) < params.grassDensity) {
                        decorationMap[idx] = static_cast<uint32_t>(TerrainType::GRASS);
                    }
                }
            }
        }
    }
    
    bool isInTreeCluster(uint32_t x, uint32_t y, const TileMap& decorationMap,
                        const MapConfig& config, const DecorationParams& params) {
        // 检查周围是否有树木形成集群
        int treeCount = 0;
        int radius = static_cast<int>(params.treeClusterSize);
        
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                uint32_t nx = x + dx;
                uint32_t ny = y + dy;
                
                if (nx < config.width && ny < config.height) {
                    uint32_t nIdx = ny * config.width + nx;
                    TerrainType deco = static_cast<TerrainType>(decorationMap[nIdx]);
                    
                    if (deco == TerrainType::TREE_DENSE || 
                        deco == TerrainType::TREE_SPARSE ||
                        deco == TerrainType::TREE_PALM ||
                        deco == TerrainType::TREE_SNOW) {
                        treeCount++;
                    }
                }
            }
        }
        
        return treeCount >= 3; // 至少3棵树形成集群
    }
    
    float calculateSlope(uint32_t x, uint32_t y, const HeightMap& heightmap,
                        const MapConfig& config) {
        if (x == 0 || x == config.width - 1 || y == 0 || y == config.height - 1) {
            return 0.0f;
        }
        
        float center = heightmap[y * config.width + x];
        float maxSlope = 0.0f;
        
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                
                uint32_t nIdx = (y + dy) * config.width + (x + dx);
                float slope = fabs(heightmap[nIdx] - center);
                maxSlope = std::max(maxSlope, slope);
            }
        }
        
        return maxSlope;
    }
    
    void calculateStatistics(MapData& data) {
        auto& stats = data.stats;
        stats.waterTiles = 0;
        stats.landTiles = 0;
        stats.forestTiles = 0;
        stats.mountainTiles = 0;
        stats.riverTiles = 0;
        
        float totalHeight = 0.0f;
        stats.minHeight = std::numeric_limits<float>::max();
        stats.maxHeight = std::numeric_limits<float>::lowest();
        
        for (size_t i = 0; i < data.heightMap.size(); ++i) {
            float height = data.heightMap[i];
            totalHeight += height;
            stats.minHeight = std::min(stats.minHeight, height);
            stats.maxHeight = std::max(stats.maxHeight, height);
            
            TerrainType terrain = static_cast<TerrainType>(data.terrainMap[i]);
            
            switch (terrain) {
                case TerrainType::DEEP_OCEAN:
                case TerrainType::SHALLOW_OCEAN:
                case TerrainType::COAST:
                case TerrainType::LAKE:
                case TerrainType::RIVER:
                    stats.waterTiles++;
                    if (terrain == TerrainType::RIVER) {
                        stats.riverTiles++;
                    }
                    break;
                default:
                    stats.landTiles++;
                    break;
            }
            
            if (terrain == TerrainType::FOREST) {
                stats.forestTiles++;
            } else if (terrain == TerrainType::MOUNTAIN ||
                      terrain == TerrainType::SNOW_MOUNTAIN) {
                stats.mountainTiles++;
            }
        }
        
        stats.averageHeight = totalHeight / data.heightMap.size();
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

TileMap MapGeneratorInternal::generateDecorationOnly(const HeightMap& heightmap,
                                                    const TileMap& terrainMap,
                                                    const MapConfig& config) {
    return m_impl->generateDecorationOnly(heightmap, terrainMap, config);
}

void MapGeneratorInternal::applyErosion(HeightMap& heightmap, const MapConfig& config,
                                       const ErosionParams& params) {
    m_impl->applyErosion(heightmap, config, params);
}

void MapGeneratorInternal::generateRivers(TileMap& terrainMap, const HeightMap& heightmap,
                                         const MapConfig& config, const RiverParams& params) {
    m_impl->generateRivers(terrainMap, heightmap, config, params);
}

void MapGeneratorInternal::addDecorations(TileMap& decorationMap, const TileMap& terrainMap,
                                         const HeightMap& heightmap, const MapConfig& config,
                                         const DecorationParams& params) {
    m_impl->addDecorations(decorationMap, terrainMap, heightmap, config, params);
}

} // namespace internal
} // namespace MapGenerator

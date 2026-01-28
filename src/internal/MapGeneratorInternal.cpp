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
#define _USE_MATH_DEFINES
#include <cmath>
#include <queue>

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
        addReedsDecorations(decorationMap, terrainMap, heightmap, config, params);
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
    
    WFCParams createWFCParamsFromConfig(const MapConfig& config) {
        WFCParams params;
        params.iterations = config.wfcIterations;
        params.entropyWeight = config.wfcEntropyWeight;
        params.enableBacktracking = config.wfcEnableBacktracking;
        params.temperature = config.temperature;
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
            default:
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
        std::vector<RiverPoint> mainRiver;
        std::queue<RiverPoint> riverQueue;

        // 初始化起点
        RiverPoint startPoint{startX, startY, heightmap[startY * config.width + startX], false, 0};
        riverQueue.push(startPoint);

        std::mt19937 riverRng(m_seed + startX * 1000 + startY);
        std::uniform_real_distribution<float> stopDist(0.0f, 1.0f);

        // 使用迭代代替递归
        while (!riverQueue.empty()) {
            RiverPoint current = riverQueue.front();
            riverQueue.pop();

            // 检查递归深度限制
            if (current.depth > 500) {
                continue; // 防止过深的递归
            }

            uint32_t x = current.x;
            uint32_t y = current.y;

            // 检查边界
            if (x == 0 || x >= config.width - 1 || y == 0 || y >= config.height - 1) {
                continue;
            }

            uint32_t idx = y * config.width + x;

            // 保存当前点
            mainRiver.push_back(current);

            // 标记为河流
            TerrainType currentTerrain = static_cast<TerrainType>(terrainMap[idx]);
            if (currentTerrain != TerrainType::DEEP_OCEAN &&
                currentTerrain != TerrainType::SHALLOW_OCEAN) {
                terrainMap[idx] = static_cast<uint32_t>(TerrainType::RIVER);
            }

            // 检查是否到达海洋
            if (currentTerrain == TerrainType::DEEP_OCEAN ||
                currentTerrain == TerrainType::SHALLOW_OCEAN) {
                break; // 河流流入海洋，结束
            }

            // 随机终止（模拟蒸发）
            if (stopDist(riverRng) < 0.01f) { // 1%概率终止
                break;
            }

            // 检查河流长度限制
            if (mainRiver.size() > params.maxRiverLength) {
                break;
            }

            // 找到最低的邻居（水流方向）
            float minHeight = current.height;
            int bestDx = 0, bestDy = 0;
            bool foundLower = false;

            // 优先检查直接邻居（4方向）
            std::vector<std::pair<int, int>> directions = {
                {0, -1}, {0, 1}, {-1, 0}, {1, 0}, // 上下左右
                {-1, -1}, {-1, 1}, {1, -1}, {1, 1} // 对角线
            };

            for (const auto& [dx, dy] : directions) {
                uint32_t nx = x + dx;
                uint32_t ny = y + dy;

                if (nx < config.width && ny < config.height) {
                    uint32_t nIdx = ny * config.width + nx;
                    float nHeight = heightmap[nIdx];

                    if (nHeight < minHeight) {
                        minHeight = nHeight;
                        bestDx = dx;
                        bestDy = dy;
                        foundLower = true;
                    }
                }
            }

            // 如果没有找到更低点，尝试找相似高度点
            if (!foundLower) {
                float currentHeight = heightmap[idx];
                for (const auto& [dx, dy] : directions) {
                    uint32_t nx = x + dx;
                    uint32_t ny = y + dy;

                    if (nx < config.width && ny < config.height) {
                        uint32_t nIdx = ny * config.width + nx;
                        float nHeight = heightmap[nIdx];

                        // 允许在平坦区域稍微向上游
                        if (nHeight <= currentHeight + 0.01f && nHeight < minHeight + 0.05f) {
                            minHeight = nHeight;
                            bestDx = dx;
                            bestDy = dy;
                            foundLower = true;
                        }
                    }
                }
            }

            // 如果找不到合适的下一点，河流结束
            if (!foundLower || (bestDx == 0 && bestDy == 0)) {
                // 尝试创建终点湖泊
                if (mainRiver.size() > 10 && stopDist(riverRng) < 0.3f) {
                    createTerminalLake(terrainMap, config, x, y, params);
                }
                break;
            }

            // 移动到下一个点
            uint32_t nextX = x + bestDx;
            uint32_t nextY = y + bestDy;
            uint32_t nextIdx = nextY * config.width + nextX;

            // 检查是否形成环
            bool formsLoop = false;
            for (const auto& point : mainRiver) {
                if (point.x == nextX && point.y == nextY) {
                    formsLoop = true;
                    break;
                }
            }

            if (formsLoop) {
                break; // 避免形成循环
            }

            // 添加到队列继续处理
            RiverPoint nextPoint{
                nextX,
                nextY,
                heightmap[nextIdx],
                current.isTributary,
                current.depth + 1
            };
            riverQueue.push(nextPoint);

            // 检查是否需要生成支流
            if (params.tributaries && !current.isTributary &&
                mainRiver.size() > 20 && mainRiver.size() % 30 == 0) {
                generateTributaryFromPoint(terrainMap, heightmap, config,
                                           x, y, current.height, params,
                                           current.depth + 1);
            }
        }

        // 生成支流（如果主河流足够长）
        if (params.tributaries && mainRiver.size() > 30) {
            generateTributariesIterative(terrainMap, heightmap, config, mainRiver, params);
        }
    }

    void generateTributariesIterative(TileMap& terrainMap, const HeightMap& heightmap,
                                      const MapConfig& config,
                                      const std::vector<RiverPoint>& mainRiver,
                                      const RiverParams& params) {
        std::mt19937 tribRng(m_seed + 789);
        std::uniform_real_distribution<float> angleDist(params.minTributaryAngle,
                                                        params.maxTributaryAngle);
        std::uniform_int_distribution<uint32_t> pointDist(10, mainRiver.size() - 10);
        std::uniform_real_distribution<float> probDist(0.0f, 1.0f);

        // 限制支流数量
        uint32_t maxTributaries = std::min(5u, static_cast<uint32_t>(mainRiver.size() / 30));
        uint32_t numTributaries = 0;

        for (uint32_t i = 0; i < maxTributaries && numTributaries < 3; i++) {
            uint32_t startPointIdx = pointDist(tribRng);

            // 确保不在河流的开头或结尾附近
            if (startPointIdx < 10 || startPointIdx > mainRiver.size() - 10) {
                continue;
            }

            const RiverPoint& startPoint = mainRiver[startPointIdx];

            // 50%概率在这个点生成支流
            if (probDist(tribRng) > 0.5f) {
                continue;
            }

            float angle = angleDist(tribRng) * M_PI / 180.0f;

            // 从主河道偏移开始支流
            int offsetX = static_cast<int>(cos(angle) * 8);
            int offsetY = static_cast<int>(sin(angle) * 8);

            uint32_t tribStartX = std::clamp(static_cast<int>(startPoint.x) + offsetX,
                                             1, static_cast<int>(config.width) - 2);
            uint32_t tribStartY = std::clamp(static_cast<int>(startPoint.y) + offsetY,
                                             1, static_cast<int>(config.height) - 2);

            // 确保起点在陆地上
            uint32_t startIdx = tribStartY * config.width + tribStartX;
            TerrainType startTerrain = static_cast<TerrainType>(terrainMap[startIdx]);
            if (startTerrain == TerrainType::DEEP_OCEAN ||
                startTerrain == TerrainType::SHALLOW_OCEAN ||
                startTerrain == TerrainType::LAKE) {
                continue;
            }

            // 确保起点高度合适
            float startHeight = heightmap[startIdx];
            if (startHeight < params.minSourceHeight || startHeight > params.maxSourceHeight) {
                continue;
            }

            // 使用非递归方式生成支流
            generateTributaryFromPoint(terrainMap, heightmap, config,
                                       tribStartX, tribStartY, startHeight, params, 0);

            numTributaries++;
        }
    }

    void generateTributaryFromPoint(TileMap& terrainMap, const HeightMap& heightmap,
                                    const MapConfig& config, uint32_t startX, uint32_t startY,
                                    float startHeight, const RiverParams& params, uint32_t depth) {
        // 限制支流深度
        if (depth > 3) { // 最多3级支流
            return;
        }

        struct TributaryPoint {
            uint32_t x;
            uint32_t y;
            uint32_t depth;
        };

        std::queue<TributaryPoint> tribQueue;
        tribQueue.push({startX, startY, depth});

        std::mt19937 localRng(m_seed + startX * 10000 + startY);
        std::uniform_real_distribution<float> stopDist(0.0f, 1.0f);

        while (!tribQueue.empty()) {
            TributaryPoint current = tribQueue.front();
            tribQueue.pop();

            uint32_t x = current.x;
            uint32_t y = current.y;

            // 检查边界
            if (x == 0 || x >= config.width - 1 || y == 0 || y >= config.height - 1) {
                continue;
            }

            uint32_t idx = y * config.width + x;

            // 标记为河流
            TerrainType currentTerrain = static_cast<TerrainType>(terrainMap[idx]);
            if (currentTerrain != TerrainType::DEEP_OCEAN &&
                currentTerrain != TerrainType::SHALLOW_OCEAN &&
                currentTerrain != TerrainType::LAKE) {
                terrainMap[idx] = static_cast<uint32_t>(TerrainType::RIVER);
            }

            // 检查是否连接到主河流或海洋
            bool connected = false;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;

                    uint32_t nx = x + dx;
                    uint32_t ny = y + dy;

                    if (nx < config.width && ny < config.height) {
                        uint32_t nIdx = ny * config.width + nx;
                        TerrainType neighbor = static_cast<TerrainType>(terrainMap[nIdx]);

                        if (neighbor == TerrainType::RIVER ||
                            neighbor == TerrainType::DEEP_OCEAN ||
                            neighbor == TerrainType::SHALLOW_OCEAN) {
                            connected = true;
                            break;
                        }
                    }
                }
                if (connected) break;
            }

            if (connected) {
                break; // 支流已连接到水系
            }

            // 随机终止
            if (stopDist(localRng) < 0.05f) { // 5%概率终止
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
                break; // 找不到下坡路
            }

            // 移动到下一个点
            uint32_t nextX = x + bestDx;
            uint32_t nextY = y + bestDy;

            // 检查递归深度
            if (current.depth + 1 > depth + 10) { // 限制支流长度
                break;
            }

            tribQueue.push({nextX, nextY, current.depth + 1});
        }
    }

    void createTerminalLake(TileMap& terrainMap, const MapConfig& config,
                            uint32_t centerX, uint32_t centerY, const RiverParams& params) {
        std::mt19937 lakeRng(m_seed + centerX * 100 + centerY);
        std::uniform_real_distribution<float> sizeDist(params.minLakeSize * 0.5f,
                                                       params.maxLakeSize * 0.8f);

        float lakeSize = sizeDist(lakeRng);

        for (int dy = -static_cast<int>(lakeSize); dy <= static_cast<int>(lakeSize); dy++) {
            for (int dx = -static_cast<int>(lakeSize); dx <= static_cast<int>(lakeSize); dx++) {
                uint32_t x = centerX + dx;
                uint32_t y = centerY + dy;

                if (x < config.width && y < config.height) {
                    float dist = sqrt(dx * dx + dy * dy);
                    if (dist <= lakeSize) {
                        uint32_t idx = y * config.width + x;
                        TerrainType current = static_cast<TerrainType>(terrainMap[idx]);

                        // 只覆盖陆地
                        if (current != TerrainType::DEEP_OCEAN &&
                            current != TerrainType::SHALLOW_OCEAN &&
                            current != TerrainType::COAST) {
                            terrainMap[idx] = static_cast<uint32_t>(TerrainType::LAKE);
                        }
                    }
                }
            }
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

        float baseSize = sizeDist(lakeShapeRng);

        // 使用噪声生成不规则的湖泊形状
        std::uniform_real_distribution<float> noiseDist(0.0f, 1.0f);

        // 生成随机参数用于形状变化
        float irregularity = 0.3f + noiseDist(lakeShapeRng) * 0.4f; // 不规则度 0.3-0.7
        float distortion = 0.2f + noiseDist(lakeShapeRng) * 0.3f;   // 扭曲度 0.2-0.5
        int lobes = 5 + static_cast<int>(noiseDist(lakeShapeRng) * 5); // 5-10个瓣

        // 湖泊类型：圆形、椭圆形、不规则形
        enum LakeType { CIRCULAR, ELLIPTICAL, IRREGULAR };
        LakeType lakeType = static_cast<LakeType>(static_cast<int>(noiseDist(lakeShapeRng) * 3));

        for (int dy = -static_cast<int>(baseSize * 1.5f); dy <= static_cast<int>(baseSize * 1.5f); dy++) {
            for (int dx = -static_cast<int>(baseSize * 1.5f); dx <= static_cast<int>(baseSize * 1.5f); dx++) {
                uint32_t x = centerX + dx;
                uint32_t y = centerY + dy;

                if (x < config.width && y < config.height) {
                    float dist = sqrt(dx * dx + dy * dy);

                    // 基本形状：圆形或椭圆形
                    float normalizedDist;
                    if (lakeType == ELLIPTICAL) {
                        // 椭圆形：x和y方向有不同的半径
                        float rx = baseSize * (0.8f + noiseDist(lakeShapeRng) * 0.4f);
                        float ry = baseSize * (0.8f + noiseDist(lakeShapeRng) * 0.4f);
                        normalizedDist = sqrt((dx*dx)/(rx*rx) + (dy*dy)/(ry*ry));
                    } else {
                        normalizedDist = dist / baseSize;
                    }

                    // 应用不规则性
                    float angle = atan2(dy, dx);
                    float noiseValue = 0.0f;

                    switch (lakeType) {
                    case CIRCULAR:
                        // 圆形湖泊，带有轻微不规则
                        noiseValue = (sin(angle * lobes) * 0.1f + 1.0f) * irregularity;
                        break;

                    case ELLIPTICAL:
                        // 椭圆形湖泊，带有波纹边缘
                        noiseValue = (sin(angle * 8 + dist * 0.2f) * 0.15f + 1.0f) * irregularity;
                        break;

                    case IRREGULAR:
                        // 高度不规则的湖泊
                        noiseValue = (sin(angle * lobes) * 0.2f +
                                      sin(angle * lobes * 2 + dist * 0.3f) * 0.15f +
                                      sin(dist * 0.5f) * 0.1f + 1.0f) * irregularity;
                        break;
                    }

                    // 应用柏林噪声增加细节
                    float nx = x / 10.0f;
                    float ny = y / 10.0f;
                    float perlinNoise = m_noiseGen->applyPerlinNoise(nx, ny) * 0.5f + 0.5f;
                    noiseValue *= (0.7f + perlinNoise * 0.3f);

                    // 添加随机扰动
                    std::mt19937 localRng(m_seed + x * 1000 + y);
                    std::uniform_real_distribution<float> localDist(0.0f, 1.0f);
                    float randomDistortion = 1.0f + (localDist(localRng) - 0.5f) * distortion * 2.0f;

                    // 最终距离计算
                    float finalThreshold = 1.0f * noiseValue * randomDistortion;

                    // 使用平滑过渡
                    float alpha = 1.0f - smoothstep(finalThreshold - 0.3f, finalThreshold + 0.3f, normalizedDist);

                    // 如果这个位置在湖泊内（使用概率而不是硬边界）
                    if (alpha > 0.5f) {
                        uint32_t idx = y * config.width + x;
                        TerrainType current = static_cast<TerrainType>(terrainMap[idx]);

                        // 只覆盖陆地，并且不是河流
                        if (current != TerrainType::DEEP_OCEAN &&
                            current != TerrainType::SHALLOW_OCEAN &&
                            current != TerrainType::COAST &&
                            current != TerrainType::RIVER) {

                            // 根据alpha值决定是湖泊还是浅滩
                            if (alpha > 0.8f) {
                                terrainMap[idx] = static_cast<uint32_t>(TerrainType::LAKE);
                            } else {
                                // 边缘区域可能是浅滩
                                std::uniform_real_distribution<float> shallowDist(0.0f, 1.0f);
                                if (shallowDist(localRng) < 0.3f) {
                                    terrainMap[idx] = static_cast<uint32_t>(TerrainType::BEACH);
                                } else {
                                    terrainMap[idx] = static_cast<uint32_t>(TerrainType::LAKE);
                                }
                            }

                            // 随机添加小岛
                            if (alpha < 0.95f && localDist(localRng) < 0.02f) {
                                terrainMap[idx] = static_cast<uint32_t>(TerrainType::PLAIN);
                            }
                        }
                    }
                }
            }
        }

        // 后处理：平滑湖泊边界
        // smoothLakeBoundary(terrainMap, config, centerX, centerY, baseSize);
    }

    void smoothLakeBoundary(TileMap& terrainMap, const MapConfig& config,
                            uint32_t centerX, uint32_t centerY, float lakeSize) {
        TileMap tempMap = terrainMap;

        int radius = static_cast<int>(lakeSize) + 2;

        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                uint32_t x = centerX + dx;
                uint32_t y = centerY + dy;

                if (x >= config.width || y >= config.height) continue;

                uint32_t idx = y * config.width + x;
                TerrainType current = static_cast<TerrainType>(terrainMap[idx]);

                if (current == TerrainType::LAKE) {
                    // 检查周围8个邻居
                    int lakeNeighbors = 0;
                    int totalNeighbors = 0;

                    for (int ndy = -1; ndy <= 1; ndy++) {
                        for (int ndx = -1; ndx <= 1; ndx++) {
                            if (ndx == 0 && ndy == 0) continue;

                            uint32_t nx = x + ndx;
                            uint32_t ny = y + ndy;

                            if (nx < config.width && ny < config.height) {
                                totalNeighbors++;
                                uint32_t nIdx = ny * config.width + nx;
                                TerrainType neighbor = static_cast<TerrainType>(terrainMap[nIdx]);

                                if (neighbor == TerrainType::LAKE) {
                                    lakeNeighbors++;
                                }
                            }
                        }
                    }

                    // 如果湖泊单元格被陆地包围太多，可能是孤岛
                    if (lakeNeighbors < 3 && totalNeighbors > 0) {
                        float lakeRatio = static_cast<float>(lakeNeighbors) / totalNeighbors;
                        if (lakeRatio < 0.4f) {
                            tempMap[idx] = static_cast<uint32_t>(TerrainType::PLAIN);
                        }
                    }
                }
            }
        }

        terrainMap = tempMap;
    }

    // 平滑步进函数（如果Utils中没有）
    float smoothstep(float edge0, float edge1, float x) {
        x = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
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

    void addReedsDecorations(TileMap& decorationMap,
                            const TileMap& terrainMap,
                            const HeightMap& heightmap,
                            const MapConfig& config,
                            const DecorationParams& params) {
        std::mt19937 reedsRng(m_seed + 444);

        // 第一遍：标记可能生成芦苇的位置
        std::vector<bool> canHaveReeds(config.width * config.height, false);
        std::vector<float> reedsProbabilities(config.width * config.height, 0.0f);

        for (uint32_t y = 0; y < config.height; y++) {
            for (uint32_t x = 0; x < config.width; x++) {
                uint32_t idx = y * config.width + x;
                TerrainType terrain = static_cast<TerrainType>(terrainMap[idx]);

                if (shouldHaveReeds(terrain, static_cast<TerrainType>(decorationMap[idx]),
                                    x, y, decorationMap, terrainMap, config)) {
                    canHaveReeds[idx] = true;
                    reedsProbabilities[idx] = calculateReedsProbability(terrain, heightmap[idx],
                                                                        x, y, config);
                }
            }
        }

        // 第二遍：生成芦苇，考虑集群效果
        for (uint32_t y = 0; y < config.height; y++) {
            for (uint32_t x = 0; x < config.width; x++) {
                uint32_t idx = y * config.width + x;

                if (canHaveReeds[idx]) {
                    float clusterBonus = calculateReedsClusterBonus(x, y, canHaveReeds,
                                                                    reedsProbabilities, config);
                    float finalProbability = reedsProbabilities[idx] * clusterBonus;

                    std::uniform_real_distribution<float> reedsDist(0.0f, 1.0f);
                    if (reedsDist(reedsRng) < finalProbability) {
                        // 检查最小间距
                        if (isValidReedsLocation(x, y, decorationMap, config, params)) {
                            decorationMap[idx] = static_cast<uint32_t>(TerrainType::REEDS);
                        }
                    }
                }
            }
        }
    }

    float calculateReedsClusterBonus(uint32_t x, uint32_t y,
                                    const std::vector<bool>& canHaveReeds,
                                    const std::vector<float>& probabilities,
                                    const MapConfig& config) {
        // 检查周围是否有其他芦苇或可能的位置
        int reedsNeighbors = 0;
        int possibleNeighbors = 0;

        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx == 0 && dy == 0) continue;

                uint32_t nx = x + dx;
                uint32_t ny = y + dy;

                if (nx < config.width && ny < config.height) {
                    uint32_t nIdx = ny * config.width + nx;

                    if (canHaveReeds[nIdx]) {
                        possibleNeighbors++;
                        // 如果邻居已经有芦苇或概率很高
                        if (probabilities[nIdx] > 0.5f) {
                            reedsNeighbors++;
                        }
                    }
                }
            }
        }

        if (possibleNeighbors == 0) return 1.0f;

        // 集群奖励：周围有越多芦苇，当前位置也越可能有
        float clusterRatio = static_cast<float>(reedsNeighbors) / possibleNeighbors;
        return 1.0f + clusterRatio * 2.0f;  // 最多3倍概率
    }

    bool isValidReedsLocation(uint32_t x, uint32_t y,
                            const TileMap& decorationMap,
                            const MapConfig& config,
                            const DecorationParams& params) {
        // 检查最小间距
        float minSpacing = params.minDecorationSpacing * 0.5f;  // 芦苇可以更密集

        for (int dy = -static_cast<int>(minSpacing); dy <= static_cast<int>(minSpacing); dy++) {
            for (int dx = -static_cast<int>(minSpacing); dx <= static_cast<int>(minSpacing); dx++) {
                if (dx == 0 && dy == 0) continue;

                uint32_t nx = x + dx;
                uint32_t ny = y + dy;

                if (nx < config.width && ny < config.height) {
                    uint32_t nIdx = ny * config.width + nx;
                    TerrainType neighborDeco = static_cast<TerrainType>(decorationMap[nIdx]);

                    if (neighborDeco == TerrainType::REEDS) {
                        // 已经有芦苇，太近了
                        float distance = sqrt(dx*dx + dy*dy);
                        if (distance < minSpacing) {
                            return false;
                        }
                    }
                }
            }
        }

        return true;
    }

    bool shouldHaveReeds(TerrainType terrain,
                         TerrainType currentDecoration,
                         uint32_t x, uint32_t y,
                         const TileMap& decorationMap,
                         const TileMap& terrainMap,
                         const MapConfig& config) {
        // 芦苇应该出现在：
        // 1. 沼泽地形
        // 2. 河流/湖泊岸边
        // 3. 浅海区域

        // 当前装饰已经是水，可以在上面加芦苇
        if (currentDecoration == TerrainType::WATER) {
            // 检查是否是岸边（有水也有陆地邻居）
            return isWaterEdge(x, y, decorationMap, config);
        }

        // 沼泽地形
        if (terrain == TerrainType::SWAMP) {
            return true;
        }

        // 海岸线
        if (terrain == TerrainType::COAST) {
            return true;
        }

        return false;
    }

    float calculateReedsProbability(TerrainType terrain,
                                    float height,
                                    uint32_t x, uint32_t y,
                                    const MapConfig& config) {
        float baseProbability = 0.0f;

        switch (terrain) {
        case TerrainType::SWAMP:
            baseProbability = 0.6f;  // 沼泽地芦苇概率高
            break;
        case TerrainType::RIVER:
        case TerrainType::LAKE:
            baseProbability = 0.4f;  // 河边湖边中等概率
            break;
        case TerrainType::COAST:
            baseProbability = 0.3f;  // 海岸线较低概率
            break;
        case TerrainType::SHALLOW_OCEAN:
            baseProbability = 0.2f;  // 浅海低概率
            break;
        default:
            return 0.0f;
        }

        // 湿度影响（如果可获取）
        float moisture = calculateMoisture(x, y, config, height);
        baseProbability *= (0.5f + moisture * 0.5f);  // 湿度越高芦苇越多

        // 高度影响：低洼地区芦苇更多
        if (height < 0.4f) {
            baseProbability *= 1.5f;
        }

        // 气候影响
        switch (config.climate) {
        case ClimateType::TROPICAL:
            baseProbability *= 1.3f;  // 热带地区芦苇更多
            break;
        case ClimateType::ARID:
            baseProbability *= 0.3f;  // 干旱地区芦苇很少
            break;
        default:
            break;
        }

        return std::min(baseProbability, 0.8f);  // 上限80%
    }

    bool isWaterEdge(uint32_t x, uint32_t y,
                                                 const TileMap& decorationMap,
                                                 const MapConfig& config) {
        // 检查当前水单元格是否有陆地邻居
        uint32_t idx = y * config.width + x;

        // 确保当前单元格是水
        if (static_cast<TerrainType>(decorationMap[idx]) != TerrainType::WATER) {
            return false;
        }

        int landNeighbors = 0;
        int waterNeighbors = 0;

        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;

                uint32_t nx = x + dx;
                uint32_t ny = y + dy;

                if (nx < config.width && ny < config.height) {
                    uint32_t nIdx = ny * config.width + nx;
                    TerrainType neighborDeco = static_cast<TerrainType>(decorationMap[nIdx]);

                    if (neighborDeco == TerrainType::WATER) {
                        waterNeighbors++;
                    } else {
                        landNeighbors++;
                    }
                }
            }
        }

        // 如果是水边（有陆地邻居）
        return landNeighbors > 0;
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

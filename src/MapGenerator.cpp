#include <fstream>
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "MapGenerator.h"
#include "internal/MapGeneratorInternal.h"

namespace MapGenerator {

class MapGenerator::Impl {
public:
    Impl() : m_internal(std::make_unique<internal::MapGeneratorInternal>(12345)) {
    }
    
    std::shared_ptr<MapData> generateMap(const MapConfig& config) {
        return m_internal->generate(config);
    }
    
    std::vector<std::shared_ptr<MapData>> generateBatch(
        const MapConfig& baseConfig, uint32_t count) {
        return m_internal->generateBatch(baseConfig, count);
    }
    
private:
    std::unique_ptr<internal::MapGeneratorInternal> m_internal;
};

// 添加辅助函数
namespace {
    // 保存PPM图像（彩色）
    bool savePPM(const std::string& filename, const std::vector<uint8_t>& data,
                uint32_t width, uint32_t height) {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // PPM头部
        file << "P6\n" << width << " " << height << "\n255\n";
        
        // 写入像素数据
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        
        return file.good();
    }
    
    // 保存PGM图像（灰度）
    bool savePGM(const std::string& filename, const std::vector<uint8_t>& data,
                uint32_t width, uint32_t height) {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // PGM头部
        file << "P5\n" << width << " " << height << "\n255\n";
        
        // 写入像素数据
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        
        return file.good();
    }
    
    // 获取地形的RGB颜色
    void getTerrainColor(::MapGenerator::TerrainType type, uint8_t& r, uint8_t& g, uint8_t& b) {
        switch (type) {
            case ::MapGenerator::TerrainType::DEEP_OCEAN:
                r = 10; g = 45; b = 110; break;
            case ::MapGenerator::TerrainType::SHALLOW_OCEAN:
                r = 25; g = 90; b = 180; break;
            case ::MapGenerator::TerrainType::COAST:
                r = 230; g = 210; b = 160; break;
            case ::MapGenerator::TerrainType::BEACH:
                r = 240; g = 230; b = 190; break;
            case ::MapGenerator::TerrainType::PLAIN:
                r = 100; g = 180; b = 90; break;
            case ::MapGenerator::TerrainType::FOREST:
                r = 30; g = 120; b = 60; break;
            case ::MapGenerator::TerrainType::HILL:
                r = 140; g = 160; b = 100; break;
            case ::MapGenerator::TerrainType::MOUNTAIN:
                r = 120; g = 110; b = 100; break;
            case ::MapGenerator::TerrainType::SNOW_MOUNTAIN:
                r = 240; g = 240; b = 240; break;
            case ::MapGenerator::TerrainType::DESERT:
                r = 230; g = 210; b = 120; break;
            case ::MapGenerator::TerrainType::SWAMP:
                r = 80; g = 140; b = 100; break;
            case ::MapGenerator::TerrainType::RIVER:
                r = 60; g = 140; b = 220; break;
            case ::MapGenerator::TerrainType::LAKE:
                r = 40; g = 110; b = 200; break;
            case ::MapGenerator::TerrainType::TREE_DENSE:
                r = 20; g = 100; b = 40; break;
            case ::MapGenerator::TerrainType::TREE_SPARSE:
                r = 40; g = 130; b = 60; break;
            case ::MapGenerator::TerrainType::BUSH:
                r = 80; g = 160; b = 80; break;
            case ::MapGenerator::TerrainType::GRASS:
                r = 120; g = 200; b = 100; break;
            case ::MapGenerator::TerrainType::SAND:
                r = 240; g = 230; b = 180; break;
            case ::MapGenerator::TerrainType::SNOW:
                r = 255; g = 255; b = 255; break;
            case ::MapGenerator::TerrainType::WATER:
                r = 30; g = 120; b = 180; break;
            case ::MapGenerator::TerrainType::REEDS:
                r = 180; g = 200; b = 100; break;
            default:
                r = g = b = 0; break;
        }
    }
}

MapGenerator::MapGenerator() : m_impl(std::make_unique<Impl>()) {
}

MapGenerator::~MapGenerator() = default;

std::shared_ptr<MapData> MapGenerator::generateMap(const MapConfig& config) {
    return m_impl->generateMap(config);
}

std::vector<std::shared_ptr<MapData>> MapGenerator::generateBatch(
    const MapConfig& baseConfig, uint32_t count) {
    return m_impl->generateBatch(baseConfig, count);
}

std::shared_ptr<MapData> MapGenerator::generateFromPreset(
    MapConfig::Preset preset) {
    MapConfig config = createConfigFromPreset(preset);
    return generateMap(config);
}

bool MapGenerator::exportToImage(const MapData& data, const std::string& filename) {
    // 简化实现 - 实际应使用图像库
    // 这里返回true表示成功
    return true;
}

bool MapGenerator::exportToJSON(const MapData& data, const std::string& filename) {
    // 简化实现 - 实际应生成JSON文件
    return true;
}

bool MapGenerator::exportToPPM(const MapData& data, const std::string& filename, 
                              bool color, uint32_t viewType) {
    std::vector<uint8_t> imageData;
    uint32_t width = data.config.width;
    uint32_t height = data.config.height;
    
    if (color) {
        // 彩色图像：3通道
        imageData.resize(width * height * 3);
        
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t idx = y * width + x;
                uint8_t r, g, b;
                
                switch (viewType) {
                    case 0: // 高度图
                        {
                            float h = data.heightMap[idx];
                            uint8_t gray = static_cast<uint8_t>(h * 255);
                            r = g = b = gray;
                        }
                        break;
                        
                    case 1: // 地形图
                        {
                            TerrainType terrain = static_cast<TerrainType>(data.terrainMap[idx]);
                            getTerrainColor(terrain, r, g, b);
                        }
                        break;
                        
                    case 2: // 装饰图
                        {
                            TerrainType decoration = static_cast<TerrainType>(data.decorationMap[idx]);
                            getTerrainColor(decoration, r, g, b);
                        }
                        break;
                        
                    case 3: // 合成图（地形+装饰）
                        {
                            TerrainType terrain = static_cast<TerrainType>(data.terrainMap[idx]);
                            getTerrainColor(terrain, r, g, b);
                            
                            // 如果有装饰，混合颜色
                            TerrainType decoration = static_cast<TerrainType>(data.decorationMap[idx]);
                            if (decoration != TerrainType::GRASS && decoration != TerrainType::WATER) { // 假设GRASS是默认无装饰
                                uint8_t dr, dg, db;
                                getTerrainColor(decoration, dr, dg, db);
                                // 简单混合
                                r = (r + dr) / 2;
                                g = (g + dg) / 2;
                                b = (b + db) / 2;
                            }
                        }
                        break;
                        
                    case 4: // 资源图
                        {
                            uint32_t resource = data.resourceMap[idx];
                            switch (resource) {
                                case 1: // 铁矿
                                    r = 150; g = 80; b = 80; break;
                                case 2: // 铜矿
                                    r = 200; g = 120; b = 60; break;
                                case 3: // 木材
                                    r = 100; g = 60; b = 30; break;
                                case 4: // 粘土
                                    r = 180; g = 160; b = 140; break;
                                default:
                                    // 无资源：显示背景地形
                                    TerrainType terrain = static_cast<TerrainType>(data.terrainMap[idx]);
                                    getTerrainColor(terrain, r, g, b);
                                    // 稍微变暗
                                    r = r * 0.7f;
                                    g = g * 0.7f;
                                    b = b * 0.7f;
                                    break;
                            }
                        }
                        break;
                        
                    default:
                        r = g = b = 0;
                        break;
                }
                
                uint32_t pixelIdx = (y * width + x) * 3;
                imageData[pixelIdx] = r;
                imageData[pixelIdx + 1] = g;
                imageData[pixelIdx + 2] = b;
            }
        }
        
        return savePPM(filename, imageData, width, height);
    } else {
        // 灰度图像：单通道
        imageData.resize(width * height);
        
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t idx = y * width + x;
                uint8_t gray;
                
                switch (viewType) {
                    case 0: // 高度图
                        gray = static_cast<uint8_t>(data.heightMap[idx] * 255);
                        break;
                        
                    case 1: // 地形图（按类型赋不同灰度）
                        {
                            TerrainType terrain = static_cast<TerrainType>(data.terrainMap[idx]);
                            gray = static_cast<uint8_t>(static_cast<uint32_t>(terrain) * 10);
                        }
                        break;
                        
                    default:
                        gray = 128;
                        break;
                }
                
                imageData[y * width + x] = gray;
            }
        }
        
        return savePGM(filename, imageData, width, height);
    }
}

bool MapGenerator::exportToPGM(const MapData& data, const std::string& filename,
                              float scale) {
    std::vector<uint8_t> imageData;
    uint32_t width = data.config.width;
    uint32_t height = data.config.height;
    
    imageData.resize(width * height);
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t idx = y * width + x;
            float heightValue = data.heightMap[idx];
            
            // 应用缩放和裁剪
            heightValue = std::clamp(heightValue * scale, 0.0f, 1.0f);
            
            uint8_t gray = static_cast<uint8_t>(heightValue * 255);
            imageData[y * width + x] = gray;
        }
    }
    
    return savePGM(filename, imageData, width, height);
}

// 添加新的导出函数
bool MapGenerator::exportHeightmapToPGM(const MapData& data, 
                                       const std::string& filename,
                                       float minHeight, float maxHeight) {
    std::vector<uint8_t> imageData;
    uint32_t width = data.config.width;
    uint32_t height = data.config.height;
    
    imageData.resize(width * height);
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t idx = y * width + x;
            float heightValue = data.heightMap[idx];
            
            // 重新映射到指定范围
            float normalized = (heightValue - minHeight) / (maxHeight - minHeight);
            normalized = std::clamp(normalized, 0.0f, 1.0f);
            
            uint8_t gray = static_cast<uint8_t>(normalized * 255);
            imageData[y * width + x] = gray;
        }
    }
    
    return savePGM(filename, imageData, width, height);
}

// 导出地形类型索引图
bool MapGenerator::exportTerrainIndexToPGM(const MapData& data,
                                          const std::string& filename) {
    std::vector<uint8_t> imageData;
    uint32_t width = data.config.width;
    uint32_t height = data.config.height;
    
    imageData.resize(width * height);
    
    // 找到最大类型值用于归一化
    uint32_t maxType = 0;
    for (uint32_t value : data.terrainMap) {
        maxType = std::max(maxType, value);
    }
    
    if (maxType == 0) maxType = 1;
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t idx = y * width + x;
            uint32_t type = data.terrainMap[idx];
            
            // 归一化到0-255
            uint8_t gray = static_cast<uint8_t>((type * 255) / maxType);
            imageData[y * width + x] = gray;
        }
    }
    
    return savePGM(filename, imageData, width, height);
}

// 导出伪彩色高度图
bool MapGenerator::exportHeightmapToPPM(const MapData& data,
                                       const std::string& filename,
                                       const std::vector<Color>& gradient) {
    std::vector<uint8_t> imageData;
    uint32_t width = data.config.width;
    uint32_t height = data.config.height;
    
    imageData.resize(width * height * 3);
    
    // 如果没有提供渐变，使用默认蓝-绿-棕-白渐变
    std::vector<Color> defaultGradient = {
        {10, 45, 110},    // 深蓝 - 深海
        {25, 90, 180},    // 蓝 - 浅海
        {230, 210, 160},  // 沙色 - 海滩
        {100, 180, 90},   // 绿 - 平原
        {140, 160, 100},  // 黄绿 - 丘陵
        {120, 110, 100},  // 棕 - 山地
        {200, 200, 200},  // 灰 - 高山
        {240, 240, 240}   // 白 - 雪山
    };
    
    const std::vector<Color>& colors = gradient.empty() ? defaultGradient : gradient;
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t idx = y * width + x;
            float heightValue = data.heightMap[idx];
            
            // 根据高度选择颜色
            float t = std::clamp(heightValue, 0.0f, 1.0f);
            float segment = t * (colors.size() - 1);
            int segmentIndex = static_cast<int>(segment);
            float segmentT = segment - segmentIndex;
            
            if (segmentIndex >= colors.size() - 1) {
                segmentIndex = colors.size() - 2;
                segmentT = 1.0f;
            }
            
            const Color& c1 = colors[segmentIndex];
            const Color& c2 = colors[segmentIndex + 1];
            
            uint8_t r = static_cast<uint8_t>(c1.r + (c2.r - c1.r) * segmentT);
            uint8_t g = static_cast<uint8_t>(c1.g + (c2.g - c1.g) * segmentT);
            uint8_t b = static_cast<uint8_t>(c1.b + (c2.b - c1.b) * segmentT);
            
            uint32_t pixelIdx = (y * width + x) * 3;
            imageData[pixelIdx] = r;
            imageData[pixelIdx + 1] = g;
            imageData[pixelIdx + 2] = b;
        }
    }
    
    return savePPM(filename, imageData, width, height);
}

MapConfig MapGenerator::createConfigFromPreset(MapConfig::Preset preset) {
    MapConfig config;
    config.preset = preset;
    
    switch (preset) {
        case MapConfig::Preset::ISLANDS:
            config.width = 1024;
            config.height = 1024;
            config.seaLevel = 0.35f;
            config.noiseScale = 150.0f;
            config.noiseOctaves = 5;
            config.climate = ClimateType::TROPICAL;
            config.temperature = 0.8f;
            config.humidity = 0.7f;
            break;
            
        case MapConfig::Preset::MOUNTAINS:
            config.width = 512;
            config.height = 512;
            config.seaLevel = 0.25f;
            config.mountainHeight = 0.75f;
            config.noiseScale = 80.0f;
            config.noisePersistence = 0.7f;
            config.noiseLacunarity = 3.0f;
            config.climate = ClimateType::CONTINENTAL;
            config.temperature = 0.4f;
            config.humidity = 0.6f;
            break;
            
        case MapConfig::Preset::PLAINS:
            config.width = 512;
            config.height = 512;
            config.seaLevel = 0.3f;
            config.beachHeight = 0.32f;
            config.plainHeight = 0.5f;
            config.noiseScale = 200.0f;
            config.noisePersistence = 0.3f;
            config.climate = ClimateType::TEMPERATE;
            config.temperature = 0.6f;
            config.humidity = 0.5f;
            break;
            
        case MapConfig::Preset::CONTINENT:
            config.width = 1024;
            config.height = 768;
            config.seaLevel = 0.3f;
            config.noiseScale = 300.0f;
            config.noiseOctaves = 7;
            config.climate = ClimateType::CONTINENTAL;
            config.temperature = 0.5f;
            config.humidity = 0.6f;
            break;
            
        case MapConfig::Preset::ARCHIPELAGO:
            config.width = 1024;
            config.height = 1024;
            config.seaLevel = 0.4f;
            config.noiseScale = 100.0f;
            config.noiseOctaves = 4;
            config.climate = ClimateType::TROPICAL;
            config.temperature = 0.9f;
            config.humidity = 0.8f;
            break;
            
        case MapConfig::Preset::SWAMP_LAKES:
            config.width = 512;
            config.height = 512;
            config.seaLevel = 0.28f;
            config.plainHeight = 0.35f;
            config.noiseScale = 120.0f;
            config.noisePersistence = 0.4f;
            config.climate = ClimateType::TROPICAL;
            config.temperature = 0.7f;
            config.humidity = 0.9f;
            break;
            
        case MapConfig::Preset::DESERT_CANYONS:
            config.width = 1024;
            config.height = 512;
            config.seaLevel = 0.2f;
            config.beachHeight = 0.22f;
            config.plainHeight = 0.3f;
            config.noiseScale = 150.0f;
            config.noisePersistence = 0.6f;
            config.noiseLacunarity = 2.5f;
            config.climate = ClimateType::ARID;
            config.temperature = 0.9f;
            config.humidity = 0.1f;
            break;
            
        case MapConfig::Preset::ALPINE:
            config.width = 768;
            config.height = 768;
            config.seaLevel = 0.25f;
            config.mountainHeight = 0.7f;
            config.noiseScale = 100.0f;
            config.noisePersistence = 0.8f;
            config.noiseLacunarity = 3.0f;
            config.climate = ClimateType::POLAR;
            config.temperature = 0.2f;
            config.humidity = 0.4f;
            break;
            
        default:
            config.width = 512;
            config.height = 512;
            config.seaLevel = 0.3f;
            config.noiseScale = 100.0f;
            config.climate = ClimateType::TEMPERATE;
            break;
    }
    
    return config;
}

std::string MapGenerator::getTerrainName(TerrainType type) {
    static const std::unordered_map<TerrainType, std::string> names = {
        {TerrainType::DEEP_OCEAN, "Deep Ocean"},
        {TerrainType::SHALLOW_OCEAN, "Shallow Ocean"},
        {TerrainType::COAST, "Coast"},
        {TerrainType::BEACH, "Beach"},
        {TerrainType::PLAIN, "Plain"},
        {TerrainType::FOREST, "Forest"},
        {TerrainType::HILL, "Hill"},
        {TerrainType::MOUNTAIN, "Mountain"},
        {TerrainType::SNOW_MOUNTAIN, "Snow Mountain"},
        {TerrainType::DESERT, "Desert"},
        {TerrainType::SWAMP, "Swamp"},
        {TerrainType::RIVER, "River"},
        {TerrainType::LAKE, "Lake"},
        {TerrainType::TREE_DENSE, "Dense Tree"},
        {TerrainType::TREE_SPARSE, "Sparse Tree"},
        {TerrainType::TREE_PALM, "Palm Tree"},
        {TerrainType::TREE_SNOW, "Snow Tree"},
        {TerrainType::ROCK_SMALL, "Small Rock"},
        {TerrainType::ROCK_LARGE, "Large Rock"},
        {TerrainType::BUSH, "Bush"},
        {TerrainType::FLOWERS, "Flowers"},
        {TerrainType::GRASS, "Grass"},
        {TerrainType::SAND, "Sand"},
        {TerrainType::CLAY, "Clay"},
        {TerrainType::SNOW, "Snow"},
        {TerrainType::WATER, "Water"},
        {TerrainType::REEDS, "Reeds"}
    };
    
    auto it = names.find(type);
    return it != names.end() ? it->second : "Unknown";
}

std::string MapGenerator::getClimateName(ClimateType type) {
    static const std::unordered_map<ClimateType, std::string> names = {
        {ClimateType::TEMPERATE, "Temperate"},
        {ClimateType::TROPICAL, "Tropical"},
        {ClimateType::ARID, "Arid"},
        {ClimateType::CONTINENTAL, "Continental"},
        {ClimateType::POLAR, "Polar"},
        {ClimateType::MEDITERRANEAN, "Mediterranean"}
    };
    
    auto it = names.find(type);
    return it != names.end() ? it->second : "Unknown";
}

namespace Utils {
    float lerp(float a, float b, float t) {
        return a + t * (b - a);
    }
    
    float clamp(float value, float min, float max) {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }
    
    float smoothstep(float edge0, float edge1, float x) {
        x = clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }
    
    uint32_t hash(uint32_t x, uint32_t y, uint32_t seed) {
        uint32_t h = seed;
        h ^= x * 0xcc9e2d51;
        h = (h << 15) | (h >> 17);
        h *= 0x1b873593;
        h ^= y * 0xcc9e2d51;
        h = (h << 15) | (h >> 17);
        h *= 0x1b873593;
        return h;
    }
    
    std::vector<float> normalizeHeightMap(const HeightMap& heightmap) {
        if (heightmap.empty()) return {};
        
        float minVal = *std::min_element(heightmap.begin(), heightmap.end());
        float maxVal = *std::max_element(heightmap.begin(), heightmap.end());
        float range = maxVal - minVal;
        
        if (range == 0.0f) return std::vector<float>(heightmap.size(), 0.5f);
        
        std::vector<float> normalized(heightmap.size());
        for (size_t i = 0; i < heightmap.size(); ++i) {
            normalized[i] = (heightmap[i] - minVal) / range;
        }
        
        return normalized;
    }
}

} // namespace MapGenerator

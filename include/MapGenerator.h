#ifndef MAPGENERATOR_H
#define MAPGENERATOR_H

#ifdef _WIN32
    #ifdef MG_BUILD_LIB
        #define MG_EXPORT __declspec(dllexport)
    #else
        #define MG_EXPORT __declspec(dllimport)
    #endif
#else
    #define MG_EXPORT __attribute__((visibility("default")))
#endif

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

namespace MapGenerator {

// 地貌类型
enum class TerrainType : uint32_t {
    // 基础地形
    UNKNOW_TERRAIN  = 0,    // 缺省
    DEEP_OCEAN      = 1,    // 深海
    SHALLOW_OCEAN   = 2,    // 浅海
    COAST           = 3,    // 海岸
    BEACH           = 4,    // 海滩
    PLAIN           = 5,    // 平原
    FOREST          = 6,    // 森林
    HILL            = 7,    // 山地
    MOUNTAIN        = 8,    // 山脉
    SNOW_MOUNTAIN   = 9,    // 雪山
    DESERT          = 10,   // 沙漠
    SWAMP           = 11,   // 沼泽
    RIVER           = 12,   // 河流
    LAKE            = 13,   // 湖泊
    // 装饰类型
    TREE_DENSE      = 14,
    TREE_SPARSE     = 15,
    TREE_PALM       = 16,
    TREE_SNOW       = 17,
    ROCK_SMALL      = 18,
    ROCK_LARGE      = 19,
    BUSH            = 20,
    FLOWERS         = 21,
    GRASS           = 22,
    SAND            = 23,
    CLAY            = 24,
    SNOW            = 25,
    WATER           = 26,
    REEDS           = 27
};

// 气候类型
enum class ClimateType : uint32_t {
    TEMPERATE       = 0,
    TROPICAL        = 1,
    ARID            = 2,
    CONTINENTAL     = 3,
    POLAR           = 4,
    MEDITERRANEAN   = 5
};

// 基础类型定义
using HeightMap = std::vector<float>;
using TileMap = std::vector<uint32_t>;

// 地图配置
struct MG_EXPORT MapConfig {
    // 基础参数
    uint32_t width = 512;
    uint32_t height = 512;
    uint32_t seed = 12345;
    
    // 噪声参数
    float noiseScale = 100.0f;
    int32_t noiseOctaves = 6;
    float noisePersistence = 0.5f;
    float noiseLacunarity = 2.0f;
    
    // 高度参数
    float seaLevel = 0.3f;
    float beachHeight = 0.32f;
    float plainHeight = 0.4f;
    float hillHeight = 0.6f;
    float mountainHeight = 0.8f;
    
    // 气候参数
    ClimateType climate = ClimateType::TEMPERATE;
    float temperature = 0.5f;
    float humidity = 0.5f;
    
    // WFC参数
    uint32_t wfcIterations = 1000;
    float wfcEntropyWeight = 0.1f;
    bool wfcEnableBacktracking = true;
    
    // 性能参数
    uint32_t threadCount = 4;
    
    // 预设
    enum class Preset {
        CUSTOM,
        ISLANDS,
        MOUNTAINS,
        PLAINS,
        CONTINENT,
        ARCHIPELAGO,
        SWAMP_LAKES,
        DESERT_CANYONS,
        ALPINE
    };
    Preset preset = Preset::CONTINENT;
};

// 地图数据
struct MG_EXPORT MapData {
    HeightMap heightMap;
    TileMap terrainMap;
    TileMap decorationMap;
    TileMap resourceMap;
    
    // 统计数据
    struct Statistics {
        uint32_t waterTiles;
        uint32_t landTiles;
        uint32_t forestTiles;
        uint32_t mountainTiles;
        uint32_t riverTiles;
        float averageHeight;
        float minHeight;
        float maxHeight;
    } stats;
    
    // 元数据
    MapConfig config;
    uint32_t generationTimeMs;
};

// 地图生成器主类
class MG_EXPORT MapGenerator {
public:
    MapGenerator();
    ~MapGenerator();
    
    // 禁止拷贝
    MapGenerator(const MapGenerator&) = delete;
    MapGenerator& operator=(const MapGenerator&) = delete;
    
    // 生成地图
    std::shared_ptr<MapData> generateMap(const MapConfig& config);
    
    // 批量生成
    std::vector<std::shared_ptr<MapData>> generateBatch(
        const MapConfig& baseConfig, uint32_t count);
    
    // 从预设生成
    std::shared_ptr<MapData> generateFromPreset(MapConfig::Preset preset);
    
    // 导出地图
    bool exportToImage(const MapData& data, const std::string& filename);
    bool exportToJSON(const MapData& data, const std::string& filename);

    // 导出到PPM/PGM图像
    bool exportToPPM(const MapData& data, const std::string& filename, 
                    bool color = true, uint32_t viewType = 0);
    bool exportToPGM(const MapData& data, const std::string& filename,
                    float scale = 1.0f);

    // 颜色结构体
    struct Color {
        uint8_t r, g, b;
        Color(uint8_t red = 0, uint8_t green = 0, uint8_t blue = 0) 
            : r(red), g(green), b(blue) {}
    };

    bool exportHeightmapToPGM(const MapData& data, const std::string& filename,
                            float minHeight = 0.0f, float maxHeight = 1.0f);
    bool exportTerrainIndexToPGM(const MapData& data, const std::string& filename);
    bool exportHeightmapToPPM(const MapData& data, const std::string& filename,
                            const std::vector<Color>& gradient = {});
    
    // 工具函数
    static MapConfig createConfigFromPreset(MapConfig::Preset preset);
    static std::string getTerrainName(TerrainType type);
    static std::string getClimateName(ClimateType type);
    
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

// 工具函数
namespace Utils {
    MG_EXPORT float lerp(float a, float b, float t);
    MG_EXPORT float clamp(float value, float min, float max);
    MG_EXPORT float smoothstep(float edge0, float edge1, float x);
    MG_EXPORT uint32_t hash(uint32_t x, uint32_t y, uint32_t seed);
    MG_EXPORT std::vector<float> normalizeHeightMap(const HeightMap& heightmap);
}

} // namespace MapGenerator

#endif // MAPGENERATOR_H

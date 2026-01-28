// src/internal/MapGeneratorInternal.h
#ifndef MAPGENERATOR_INTERNAL_MAPGENERATORINTERNAL_H
#define MAPGENERATOR_INTERNAL_MAPGENERATORINTERNAL_H

#include "CommonTypes.h"
#include <memory>

namespace MapGenerator {
namespace internal {

class MapGeneratorInternal {
public:
    explicit MapGeneratorInternal(uint32_t seed = 12345);
    ~MapGeneratorInternal();
    
    // 生成完整地图数据
    std::shared_ptr<MapData> generate(const MapConfig& config);
    
    // 批量生成
    std::vector<std::shared_ptr<MapData>> generateBatch(
        const MapConfig& baseConfig, uint32_t count);
    
    // 分步骤生成
    HeightMap generateHeightmapOnly(const MapConfig& config);
    TileMap generateTerrainOnly(const HeightMap& heightmap, 
                               const MapConfig& config);
    TileMap generateDecorationOnly(const HeightMap& heightmap,
                                  const TileMap& terrainMap,
                                  const MapConfig& config);
    
    // 高级功能
    void applyErosion(HeightMap& heightmap, const MapConfig& config,
                      const ErosionParams& params);
    void generateRivers(TileMap& terrainMap, const HeightMap& heightmap,
                       const MapConfig& config, const RiverParams& params);
    void addDecorations(TileMap& decorationMap, const TileMap& terrainMap,
                       const HeightMap& heightmap, const MapConfig& config,
                       const DecorationParams& params);
    
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace internal
} // namespace MapGenerator

#endif // MAPGENERATOR_INTERNAL_MAPGENERATORINTERNAL_H
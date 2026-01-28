// src/internal/WFCGenerator.h
#ifndef MAPGENERATOR_INTERNAL_WFCGENERATOR_H
#define MAPGENERATOR_INTERNAL_WFCGENERATOR_H

#include "CommonTypes.h"
#include "MapGenerator.h"

#include <unordered_map>
#include <set>

namespace MapGenerator {
namespace internal {

class WFCGenerator {
public:
    explicit WFCGenerator(uint32_t seed = 12345);
    ~WFCGenerator();
    
    // 生成装饰图
    TileMap generateDecorationMap(const HeightMap& heightmap,
                                 const TileMap& terrainMap,
                                 uint32_t width, uint32_t height,
                                 const WFCParams& params);
    
    // 生成资源分布图
    TileMap generateResourceMap(const TileMap& terrainMap,
                               const TileMap& decorationMap,
                               uint32_t width, uint32_t height,
                               const WFCParams& params);
    
    // 使用模式学习生成
    TileMap generateFromExample(const TileMap& example,
                               uint32_t exampleWidth, uint32_t exampleHeight,
                               uint32_t outputWidth, uint32_t outputHeight,
                               const WFCParams& params);
    
    // 手动规则生成
    void setRules(const std::unordered_map<TerrainType, 
                  std::set<TerrainType>>& adjacencyRules,
                  const std::unordered_map<TerrainType, float>& frequencyWeights);
    
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace internal
} // namespace MapGenerator

#endif // MAPGENERATOR_INTERNAL_WFCGENERATOR_H
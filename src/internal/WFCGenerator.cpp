#include "WFCGenerator.h"
#include <algorithm>
#include <queue>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <functional>

namespace MapGenerator {
namespace internal {

class WFCGenerator::Impl {
private:
    std::mt19937 m_rng;
    uint32_t m_seed;
    
    // WFC数据结构
    struct Pattern {
        std::vector<TerrainType> tiles;
        uint32_t width;
        uint32_t height;
        float frequency;
        
        Pattern(const std::vector<TerrainType>& t, uint32_t w, uint32_t h, float f = 1.0f)
            : tiles(t), width(w), height(h), frequency(f) {}
    };
    
    struct WFCCell {
        std::set<uint32_t> possiblePatterns;
        bool collapsed = false;
        TerrainType collapsedType = TerrainType::GRASS;
        float entropy = 0.0f;
        
        void reset(const std::set<uint32_t>& allPatterns) {
            possiblePatterns = allPatterns;
            collapsed = false;
            entropy = calculateEntropy();
        }
        
        float calculateEntropy() const {
            if (collapsed || possiblePatterns.empty()) return 0.0f;
            return log(static_cast<float>(possiblePatterns.size()));
        }
    };
    
    // 规则和模式
    std::vector<Pattern> m_patterns;
    std::unordered_map<uint32_t, std::set<uint32_t>> m_adjacencyRules; // pattern -> allowed neighbors
    std::unordered_map<TerrainType, float> m_frequencyWeights;
    std::unordered_map<TerrainType, std::set<TerrainType>> m_terrainAdjacencyRules;
    std::unordered_map<TerrainType, std::set<TerrainType>> m_terrainRequirementRules;
    
    // 从示例学习的模式
    std::unordered_map<uint32_t, uint32_t> m_patternHashes;
    
public:
    Impl(uint32_t seed) : m_rng(seed), m_seed(seed) {
        initializeDefaultRules();
        generateDefaultPatterns();
    }
    
    TileMap generateDecorationMap(const HeightMap& heightmap,
                                 const TileMap& terrainMap,
                                 uint32_t width, uint32_t height,
                                 const WFCParams& params) {
        // 生成基于地形的装饰
        if (params.useManualRules) {
            return generateWithManualRules(heightmap, terrainMap, width, height, params);
        } else {
            return generateWithLearnedPatterns(heightmap, terrainMap, width, height, params);
        }
    }
    
    TileMap generateResourceMap(const TileMap& terrainMap,
                               const TileMap& decorationMap,
                               uint32_t width, uint32_t height,
                               const WFCParams& params) {
        TileMap resourceMap(width * height, 0);
        
        // 根据地形和装饰生成资源
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                uint32_t idx = y * width + x;
                TerrainType terrain = static_cast<TerrainType>(terrainMap[idx]);
                TerrainType decoration = static_cast<TerrainType>(decorationMap[idx]);
                
                uint32_t resource = determineResource(terrain, decoration, x, y);
                resourceMap[idx] = resource;
            }
        }
        
        // 使用WFC聚类资源
        if (params.useWeights) {
            clusterResources(resourceMap, width, height, params);
        }
        
        return resourceMap;
    }
    
    TileMap generateFromExample(const TileMap& example,
                               uint32_t exampleWidth, uint32_t exampleHeight,
                               uint32_t outputWidth, uint32_t outputHeight,
                               const WFCParams& params) {
        // 从示例学习模式
        learnPatternsFromExample(example, exampleWidth, exampleHeight, params.patternSize);
        
        // 生成新地图
        return generateWithLearnedPatternsFromScratch(outputWidth, outputHeight, params);
    }
    
    void setRules(const std::unordered_map<TerrainType, std::set<TerrainType>>& adjacencyRules,
                  const std::unordered_map<TerrainType, float>& frequencyWeights) {
        m_terrainAdjacencyRules = adjacencyRules;
        m_frequencyWeights = frequencyWeights;
        
        // 根据规则生成模式
        generatePatternsFromRules();
    }
    
private:
    void initializeDefaultRules() {
        // 默认邻接规则
        std::vector<std::pair<TerrainType, std::vector<TerrainType>>> defaultRules = {
            // 森林相关
            {TerrainType::FOREST, {TerrainType::TREE_DENSE, TerrainType::TREE_SPARSE, 
                                  TerrainType::BUSH, TerrainType::GRASS}},
            // 山地相关
            {TerrainType::MOUNTAIN, {TerrainType::ROCK_LARGE, TerrainType::ROCK_SMALL, 
                                    TerrainType::SNOW}},
            {TerrainType::HILL, {TerrainType::ROCK_SMALL, TerrainType::GRASS, 
                                TerrainType::BUSH}},
            // 平原相关
            {TerrainType::PLAIN, {TerrainType::GRASS, TerrainType::FLOWERS, 
                                 TerrainType::BUSH}},
            // 沙漠相关
            {TerrainType::DESERT, {TerrainType::SAND, TerrainType::ROCK_SMALL}},
            // 沼泽相关
            {TerrainType::SWAMP, {TerrainType::BUSH, TerrainType::CLAY}},
            // 雪地相关
            {TerrainType::SNOW_MOUNTAIN, {TerrainType::SNOW, TerrainType::ROCK_LARGE}},
            // 湖泊
            {TerrainType::LAKE, {TerrainType::WATER}},
            // 河流
            {TerrainType::RIVER, {TerrainType::WATER, TerrainType::REEDS}},
        };
        
        for (const auto& rule : defaultRules) {
            m_terrainAdjacencyRules[rule.first].insert(rule.second.begin(), rule.second.end());
        }
        
        // 默认频率权重
        m_frequencyWeights = {
            {TerrainType::GRASS, 0.4f},
            {TerrainType::TREE_DENSE, 0.2f},
            {TerrainType::TREE_SPARSE, 0.3f},
            {TerrainType::BUSH, 0.1f},
            {TerrainType::FLOWERS, 0.05f},
            {TerrainType::ROCK_SMALL, 0.1f},
            {TerrainType::ROCK_LARGE, 0.05f},
            {TerrainType::SAND, 0.5f},
            {TerrainType::SNOW, 0.3f},
            {TerrainType::CLAY, 0.2f}
        };
        
        // 需求规则
        m_terrainRequirementRules = {
            {TerrainType::FLOWERS, {TerrainType::GRASS}},
            {TerrainType::TREE_DENSE, {}},
            {TerrainType::TREE_SPARSE, {}},
            {TerrainType::BUSH, {}}
        };
    }
    
    void generateDefaultPatterns() {
        // 生成2x2模式
        generate2x2Patterns();
        
        // 生成3x3模式
        generate3x3Patterns();
        
        // 建立邻接规则
        buildPatternAdjacencyRules();
    }
    
    void generate2x2Patterns() {
        // 基本装饰类型
        std::vector<TerrainType> basicDecorations = {
            TerrainType::GRASS,
            TerrainType::TREE_SPARSE,
            TerrainType::BUSH,
            TerrainType::ROCK_SMALL,
            TerrainType::FLOWERS
        };
        
        // 生成所有2x2组合
        for (auto& t1 : basicDecorations) {
            for (auto& t2 : basicDecorations) {
                for (auto& t3 : basicDecorations) {
                    for (auto& t4 : basicDecorations) {
                        std::vector<TerrainType> pattern = {t1, t2, t3, t4};
                        
                        // 检查模式是否有效（不允许相同类型全部相同）
                        if (!(t1 == t2 && t2 == t3 && t3 == t4)) {
                            float frequency = calculatePatternFrequency(pattern);
                            m_patterns.emplace_back(pattern, 2, 2, frequency);
                        }
                    }
                }
            }
        }
    }
    
    void generate3x3Patterns() {
        // 生成中心特定的模式
        std::vector<std::pair<TerrainType, std::vector<TerrainType>>> centerPatterns = {
            {TerrainType::TREE_DENSE, {TerrainType::GRASS, TerrainType::BUSH}},
            {TerrainType::ROCK_LARGE, {TerrainType::ROCK_SMALL, TerrainType::GRASS}},
            {TerrainType::FLOWERS, {TerrainType::GRASS, TerrainType::GRASS}}
        };
        
        for (const auto& centerPattern : centerPatterns) {
            TerrainType center = centerPattern.first;
            const auto& neighbors = centerPattern.second;
            
            // 生成随机3x3模式
            for (int i = 0; i < 10; i++) { // 每种类型生成10个变体
                std::vector<TerrainType> pattern(9);
                pattern[4] = center; // 中心
                
                // 填充邻居
                std::uniform_int_distribution<> neighborDist(0, neighbors.size() - 1);
                for (int j = 0; j < 9; j++) {
                    if (j != 4) {
                        pattern[j] = neighbors[neighborDist(m_rng)];
                    }
                }
                
                float frequency = calculatePatternFrequency(pattern);
                m_patterns.emplace_back(pattern, 3, 3, frequency);
            }
        }
    }
    
    void generatePatternsFromRules() {
        m_patterns.clear();
        
        // 基于规则生成模式
        for (const auto& rule : m_terrainAdjacencyRules) {
            TerrainType center = rule.first;
            const auto& allowedNeighbors = rule.second;
            
            // 生成以center为中心的模式
            if (!allowedNeighbors.empty()) {
                std::vector<TerrainType> pattern3x3(9, center);
                
                // 随机选择邻居
                std::vector<TerrainType> neighborList(allowedNeighbors.begin(), allowedNeighbors.end());
                std::uniform_int_distribution<> neighborDist(0, neighborList.size() - 1);
                
                for (int i = 0; i < 8; i++) { // 除了中心
                    pattern3x3[i] = neighborList[neighborDist(m_rng)];
                }
                pattern3x3[4] = center; // 确保中心正确
                
                m_patterns.emplace_back(pattern3x3, 3, 3, m_frequencyWeights[center]);
            }
        }
        
        buildPatternAdjacencyRules();
    }
    
    float calculatePatternFrequency(const std::vector<TerrainType>& pattern) {
        float total = 0.0f;
        for (auto type : pattern) {
            auto it = m_frequencyWeights.find(type);
            if (it != m_frequencyWeights.end()) {
                total += it->second;
            } else {
                total += 1.0f; // 默认权重
            }
        }
        return total / pattern.size();
    }
    
    void buildPatternAdjacencyRules() {
        m_adjacencyRules.clear();
        
        // 为每个模式计算允许的邻居
        for (uint32_t i = 0; i < m_patterns.size(); i++) {
            for (uint32_t j = 0; j < m_patterns.size(); j++) {
                if (patternsCanNeighbor(i, j)) {
                    m_adjacencyRules[i].insert(j);
                }
            }
        }
    }
    
    bool patternsCanNeighbor(uint32_t patternIdxA, uint32_t patternIdxB) {
        const Pattern& a = m_patterns[patternIdxA];
        const Pattern& b = m_patterns[patternIdxB];
        
        // 简单检查：模式边界是否兼容
        // 这里简化实现，实际应该检查重叠区域
        return true; // 简化版本
    }
    
    
    void collapseCell(WFCCell& cell, float temperature) {
        if (cell.possiblePatterns.empty()) {
            cell.collapsed = true;
            cell.collapsedType = TerrainType::GRASS;
            return;
        }
        
        // 根据频率权重和温度选择模式
        std::vector<float> weights;
        std::vector<uint32_t> patterns(cell.possiblePatterns.begin(), cell.possiblePatterns.end());
        
        for (uint32_t patternIdx : patterns) {
            float weight = m_patterns[patternIdx].frequency;
            
            // 应用温度（模拟退火）
            if (temperature > 0.0f) {
                weight = std::pow(weight, 1.0f / temperature);
            }
            
            weights.push_back(weight);
        }
        
        std::discrete_distribution<> dist(weights.begin(), weights.end());
        uint32_t chosenPattern = patterns[dist(m_rng)];
        
        const Pattern& pattern = m_patterns[chosenPattern];
        cell.collapsed = true;
        cell.collapsedType = pattern.tiles[pattern.width * pattern.height / 2]; // 中心类型
        cell.possiblePatterns = {chosenPattern};
    }

    void updateEntropies(std::vector<WFCCell>& cells) {
        for (auto& cell : cells) {
            if (!cell.collapsed) {
                cell.entropy = cell.calculateEntropy();
            }
        }
    }
    
    TerrainType getBaseDecoration(TerrainType terrain, float height) {
        // 基于地形和高度返回基础装饰
        switch (terrain) {
        case TerrainType::FOREST:
            return height > 0.6f ? TerrainType::TREE_DENSE : TerrainType::TREE_SPARSE;
        case TerrainType::MOUNTAIN:
            return height > 0.8f ? TerrainType::ROCK_LARGE : TerrainType::ROCK_SMALL;
        case TerrainType::HILL:
            return TerrainType::ROCK_SMALL;
        case TerrainType::PLAIN:
            return TerrainType::GRASS;
        case TerrainType::DESERT:
            return TerrainType::SAND;
        case TerrainType::SWAMP:
            return TerrainType::BUSH;
        case TerrainType::SNOW_MOUNTAIN:
            return TerrainType::SNOW;
        case TerrainType::LAKE:
            return TerrainType::WATER;
        case TerrainType::RIVER:
            return TerrainType::WATER;
        case TerrainType::DEEP_OCEAN:
        case TerrainType::SHALLOW_OCEAN:
            return TerrainType::WATER;
        case TerrainType::COAST:
        case TerrainType::BEACH:
            return TerrainType::SAND;
        default:
            return TerrainType::GRASS;
        }
    }
    
    void applyLocalConsistency(TileMap& decorationMap, uint32_t width, uint32_t height,
                              const WFCParams& params) {
        // 应用局部一致性规则
        TileMap temp = decorationMap;
        
        for (uint32_t y = 1; y < height - 1; y++) {
            for (uint32_t x = 1; x < width - 1; x++) {
                uint32_t idx = y * width + x;
                TerrainType center = static_cast<TerrainType>(decorationMap[idx]);
                
                // 检查邻居
                int sameCount = 0;
                int totalCount = 0;
                
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        
                        uint32_t nIdx = (y + dy) * width + (x + dx);
                        TerrainType neighbor = static_cast<TerrainType>(decorationMap[nIdx]);
                        
                        // 检查是否兼容
                        if (areDecorationsCompatible(center, neighbor)) {
                            sameCount++;
                        }
                        totalCount++;
                    }
                }
                
                // 如果不兼容的邻居太多，调整装饰
                float compatibilityRatio = static_cast<float>(sameCount) / totalCount;
                if (compatibilityRatio < 0.5f) {
                    temp[idx] = static_cast<uint32_t>(findCompatibleDecoration(center, x, y, 
                                                                              decorationMap, width, height));
                }
            }
        }
        
        decorationMap = temp;
    }
    
    bool areDecorationsCompatible(TerrainType a, TerrainType b) {
        // 简化的兼容性检查
        if (a == b) return true;
        
        // 允许的组合
        static const std::set<std::pair<TerrainType, TerrainType>> compatiblePairs = {
            {TerrainType::GRASS, TerrainType::FLOWERS},
            {TerrainType::GRASS, TerrainType::BUSH},
            {TerrainType::GRASS, TerrainType::TREE_SPARSE},
            {TerrainType::TREE_DENSE, TerrainType::TREE_SPARSE},
            {TerrainType::ROCK_SMALL, TerrainType::ROCK_LARGE},
            {TerrainType::SAND, TerrainType::ROCK_SMALL}
        };
        
        return compatiblePairs.count({a, b}) > 0 || compatiblePairs.count({b, a}) > 0;
    }
    
    TerrainType findCompatibleDecoration(TerrainType current, uint32_t x, uint32_t y,
                                        const TileMap& decorationMap,
                                        uint32_t width, uint32_t height) {
        // 查找最常见的兼容装饰
        std::unordered_map<TerrainType, int> counts;
        
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                
                uint32_t nx = x + dx;
                uint32_t ny = y + dy;
                
                if (nx < width && ny < height) {
                    uint32_t nIdx = ny * width + nx;
                    TerrainType neighbor = static_cast<TerrainType>(decorationMap[nIdx]);
                    
                    if (areDecorationsCompatible(current, neighbor)) {
                        counts[neighbor]++;
                    }
                }
            }
        }
        
        // 返回最常见的兼容装饰
        TerrainType best = current;
        int bestCount = 0;
        
        for (const auto& pair : counts) {
            if (pair.second > bestCount) {
                bestCount = pair.second;
                best = pair.first;
            }
        }
        
        return best;
    }
    
    uint32_t determineResource(TerrainType terrain, TerrainType decoration, 
                              uint32_t x, uint32_t y) {
        // 基于地形和装饰确定资源类型
        uint32_t hash = (x * 73856093) ^ (y * 19349663) ^ (static_cast<uint32_t>(terrain) * 83492791);
        std::mt19937 localRng(hash);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        
        switch (terrain) {
            case TerrainType::MOUNTAIN:
                if (decoration == TerrainType::ROCK_LARGE) {
                    if (dist(localRng) < 0.15f) return 1; // 铁矿
                    if (dist(localRng) < 0.05f) return 2; // 铜矿
                }
                break;
                
            case TerrainType::FOREST:
                if (decoration == TerrainType::TREE_DENSE || 
                    decoration == TerrainType::TREE_SPARSE) {
                    if (dist(localRng) < 0.3f) return 3; // 木材
                }
                break;
                
            case TerrainType::PLAIN:
                if (decoration == TerrainType::GRASS) {
                    if (dist(localRng) < 0.1f) return 5; // 草药
                }
                break;
                
            case TerrainType::SWAMP:
                if (decoration == TerrainType::CLAY) {
                    if (dist(localRng) < 0.2f) return 4; // 粘土
                }
                break;
                
            case TerrainType::RIVER:
                if (dist(localRng) < 0.05f) return 6; // 鱼类
                break;
        }
        
        return 0; // 无资源
    }
    
    void clusterResources(TileMap& resourceMap, uint32_t width, uint32_t height,
                         const WFCParams& params) {
        // 使用WFC风格聚类资源
        TileMap clustered = resourceMap;
        
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                uint32_t idx = y * width + x;
                auto resource = resourceMap[idx];
                
                if (resource > 0) {
                    // 检查邻居
                    std::unordered_map<uint32_t, int> neighborResources;
                    
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            if (dx == 0 && dy == 0) continue;
                            
                            uint32_t nx = x + dx;
                            uint32_t ny = y + dy;
                            
                            if (nx < width && ny < height) {
                                uint32_t nIdx = ny * width + nx;
                                neighborResources[resourceMap[nIdx]]++;
                            }
                        }
                    }
                    
                    // 如果邻居中有相同资源，增强聚类
                    if (neighborResources[resource] >= 2) {
                        // 扩大资源区域
                        for (int dy = -1; dy <= 1; dy++) {
                            for (int dx = -1; dx <= 1; dx++) {
                                uint32_t nx = x + dx;
                                uint32_t ny = y + dy;
                                
                                if (nx < width && ny < height) {
                                    uint32_t nIdx = ny * width + nx;
                                    if (resourceMap[nIdx] == 0 && 
                                        neighborResources[0] > neighborResources[resource]) {
                                        clustered[nIdx] = resource;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        resourceMap = clustered;
    }
    
    void buildPatternAdjacencyRulesFromExample(const TileMap& example,
                                              uint32_t width, uint32_t height,
                                              uint32_t patternSize) {
        m_adjacencyRules.clear();
        
        // 分析示例中的模式邻接关系
        std::unordered_map<uint32_t, std::set<uint32_t>> patternNeighbors;
        
        for (uint32_t y = 0; y <= height - patternSize; y++) {
            for (uint32_t x = 0; x <= width - patternSize; x++) {
                // 提取当前模式
                std::vector<TerrainType> currentPattern;
                for (uint32_t dy = 0; dy < patternSize; dy++) {
                    for (uint32_t dx = 0; dx < patternSize; dx++) {
                        uint32_t idx = (y + dy) * width + (x + dx);
                        currentPattern.push_back(static_cast<TerrainType>(example[idx]));
                    }
                }
                
                uint32_t currentHash = hashPattern(currentPattern);
                
                // 检查右边和下边的邻居
                if (x + patternSize < width) {
                    std::vector<TerrainType> rightPattern;
                    for (uint32_t dy = 0; dy < patternSize; dy++) {
                        for (uint32_t dx = 0; dx < patternSize; dx++) {
                            uint32_t idx = (y + dy) * width + (x + dx + 1);
                            rightPattern.push_back(static_cast<TerrainType>(example[idx]));
                        }
                    }
                    
                    uint32_t rightHash = hashPattern(rightPattern);
                    patternNeighbors[currentHash].insert(rightHash);
                }
                
                if (y + patternSize < height) {
                    std::vector<TerrainType> downPattern;
                    for (uint32_t dy = 0; dy < patternSize; dy++) {
                        for (uint32_t dx = 0; dx < patternSize; dx++) {
                            uint32_t idx = (y + dy + 1) * width + (x + dx);
                            downPattern.push_back(static_cast<TerrainType>(example[idx]));
                        }
                    }
                    
                    uint32_t downHash = hashPattern(downPattern);
                    patternNeighbors[currentHash].insert(downHash);
                }
            }
        }
        
        // 转换为模式索引
        for (const auto& entry : patternNeighbors) {
            uint32_t patternHash = entry.first;
            if (m_patternHashes.find(patternHash) != m_patternHashes.end()) {
                uint32_t patternIdx = m_patternHashes[patternHash];
                
                for (uint32_t neighborHash : entry.second) {
                    if (m_patternHashes.find(neighborHash) != m_patternHashes.end()) {
                        m_adjacencyRules[patternIdx].insert(m_patternHashes[neighborHash]);
                    }
                }
            }
        }
    }
    
    void restrictNeighborPossibilities(uint32_t cellIdx, uint32_t patternIdx,
                                      std::vector<std::set<uint32_t>>& possibilities,
                                      uint32_t width, uint32_t height,
                                      const WFCParams& params) {
        uint32_t x = cellIdx % width;
        uint32_t y = cellIdx / width;
        
        const Pattern& pattern = m_patterns[patternIdx];
        
        // 限制邻居的可能性
        for (uint32_t dy = 0; dy < pattern.height; dy++) {
            for (uint32_t dx = 0; dx < pattern.width; dx++) {
                uint32_t nx = x + dx;
                uint32_t ny = y + dy;
                
                if (nx < width && ny < height) {
                    uint32_t nIdx = ny * width + nx;
                    
                    // 如果邻居还没有决定，限制其可能性
                    if (possibilities[nIdx].size() > 1) {
                        std::set<uint32_t> newPossible;
                        
                        for (uint32_t neighborPattern : possibilities[nIdx]) {
                            if (m_adjacencyRules[patternIdx].count(neighborPattern) > 0) {
                                newPossible.insert(neighborPattern);
                            }
                        }
                        
                        if (!newPossible.empty()) {
                            possibilities[nIdx] = newPossible;
                        }
                    }
                }
            }
        }
    }
    
    uint32_t hashPattern(const std::vector<TerrainType>& pattern) {
        uint32_t hash = 0;
        for (auto type : pattern) {
            hash = (hash * 31) + static_cast<uint32_t>(type);
        }
        return hash;
    }
};

// WFCGenerator公共接口实现
WFCGenerator::WFCGenerator(uint32_t seed)
    : m_impl(std::make_unique<Impl>(seed)) {
}

WFCGenerator::~WFCGenerator() = default;

void WFCGenerator::setRules(const std::unordered_map<TerrainType, 
                          std::set<TerrainType>>& adjacencyRules,
                          const std::unordered_map<TerrainType, float>& frequencyWeights) {
    m_impl->setRules(adjacencyRules, frequencyWeights);
}

} // namespace internal
} // namespace MapGenerator

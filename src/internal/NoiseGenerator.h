// src/internal/NoiseGenerator.h
#ifndef MAPGENERATOR_INTERNAL_NOISEGENERATOR_H
#define MAPGENERATOR_INTERNAL_NOISEGENERATOR_H

#include "CommonTypes.h"

namespace MapGenerator {
namespace internal {

class NoiseGenerator {
public:
    explicit NoiseGenerator(uint32_t seed = 12345);
    ~NoiseGenerator();
    
    // 生成高度图
    HeightMap generateHeightMap(uint32_t width, uint32_t height, 
                               const NoiseParams& params);
    
    // 生成特定类型的噪声
    HeightMap generateNoise(uint32_t width, uint32_t height,
                           const NoiseParams& params);
    
    // 多频混合噪声
    HeightMap generateLayeredNoise(uint32_t width, uint32_t height,
                                  const std::vector<NoiseParams::NoiseLayer>& layers);
    
    // 特殊噪声类型
    HeightMap generateRidgeNoise(uint32_t width, uint32_t height,
                                const NoiseParams& params);
    HeightMap generateTerraceNoise(uint32_t width, uint32_t height,
                                  const NoiseParams& params);
    HeightMap generateWorleyNoise(uint32_t width, uint32_t height,
                                 const NoiseParams& params);
    
    // 域扭曲
    void applyDomainWarp(HeightMap& heightmap, uint32_t width, uint32_t height,
                        const NoiseParams::DomainWarp& warp);
    
    // 后处理
    void applyErosion(HeightMap& heightmap, uint32_t width, uint32_t height,
                     const ErosionParams& params);
    void applySmoothing(HeightMap& heightmap, uint32_t width, uint32_t height,
                       uint32_t radius = 1);
    void applyTerracing(HeightMap& heightmap, uint32_t width, uint32_t height,
                       uint32_t levels);

    float applyPerlinNoise(float x, float y, float z = 0);
    
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace internal
} // namespace MapGenerator

#endif // MAPGENERATOR_INTERNAL_NOISEGENERATOR_H

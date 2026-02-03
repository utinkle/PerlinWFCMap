#include "MapGenerator.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <string>

// 测试不同预设的地图生成
void testPresets() {
    std::cout << "=== Testing Different Presets ===\n";
    
    MapGenerator::MapGenerator generator;
    
    std::vector<std::pair<MapGenerator::MapConfig::Preset, std::string>> presets = {
        {MapGenerator::MapConfig::Preset::ISLANDS, "islands"},
        {MapGenerator::MapConfig::Preset::MOUNTAINS, "mountains"},
        {MapGenerator::MapConfig::Preset::PLAINS, "plains"},
        {MapGenerator::MapConfig::Preset::CONTINENT, "continent"},
        {MapGenerator::MapConfig::Preset::ARCHIPELAGO, "archipelago"},
        {MapGenerator::MapConfig::Preset::SWAMP_LAKES, "swamp_lakes"},
        {MapGenerator::MapConfig::Preset::DESERT_CANYONS, "desert_canyons"},
        {MapGenerator::MapConfig::Preset::ALPINE, "alpine"}
    };
    
    for (const auto& preset : presets) {
        std::cout << "\nGenerating " << preset.second << " preset...\n";
        auto start = std::chrono::high_resolution_clock::now();
        
        auto map = generator.generateFromPreset(preset.first);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "Generated " << map->config.width << "x" << map->config.height 
                  << " map in " << duration.count() << "ms\n";
        
        // 导出图像
        std::string filename = preset.second + "_terrain.ppm";
        if (generator.exportToPPM(*map, filename, true, 1)) {
            std::cout << "Exported terrain map to " << filename << "\n";
        }
        
        filename = preset.second + "_height.pgm";
        if (generator.exportToPGM(*map, filename)) {
            std::cout << "Exported height map to " << filename << "\n";
        }
        
        // 输出统计信息
        std::cout << "Water/Land ratio: " 
                  << (float)map->stats.waterTiles / (map->stats.waterTiles + map->stats.landTiles) * 100 
                  << "% water\n";
        std::cout << "Height range: " << map->stats.minHeight << " - " << map->stats.maxHeight << "\n";
    }
}

// 测试自定义配置
void testCustomConfig() {
    std::cout << "\n=== Testing Custom Configuration ===\n";
    
    MapGenerator::MapGenerator generator;
    
    MapGenerator::MapConfig config;
    config.width = 512;
    config.height = 512;
    config.seed = 12345;
    config.noiseScale = 100.0f;
    config.noiseOctaves = 6;
    config.noisePersistence = 0.5f;
    config.noiseLacunarity = 2.0f;
    config.seaLevel = 0.35f;
    config.beachHeight = 0.37f;
    config.plainHeight = 0.45f;
    config.hillHeight = 0.65f;
    config.mountainHeight = 0.85f;
    config.climate = MapGenerator::ClimateType::TEMPERATE;
    config.temperature = 0.6f;
    config.humidity = 0.5f;
    
    std::cout << "Generating custom map...\n";
    auto start = std::chrono::high_resolution_clock::now();
    
    auto map = generator.generateMap(config);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Generated in " << duration.count() << "ms\n";
    
    // 导出不同视图
    generator.exportToPPM(*map, "custom_height.ppm", true, 0);
    generator.exportToPPM(*map, "custom_terrain.ppm", true, 1);
    generator.exportToPPM(*map, "custom_decoration.ppm", true, 2);
    generator.exportToPPM(*map, "custom_combined.ppm", true, 3);
    generator.exportToPPM(*map, "custom_resources.ppm", true, 4);
    generator.exportToPGM(*map, "custom_height_gray.pgm");
    
    std::cout << "Exported 6 image files for custom map\n";
    
    // 详细统计
    std::cout << "\nDetailed Statistics:\n";
    std::cout << "====================\n";
    std::cout << "Total tiles: " << map->config.width * map->config.height << "\n";
    std::cout << "Water tiles: " << map->stats.waterTiles 
              << " (" << (float)map->stats.waterTiles / (map->config.width * map->config.height) * 100 << "%)\n";
    std::cout << "Land tiles: " << map->stats.landTiles 
              << " (" << (float)map->stats.landTiles / (map->config.width * map->config.height) * 100 << "%)\n";
    std::cout << "Forest tiles: " << map->stats.forestTiles 
              << " (" << (float)map->stats.forestTiles / map->stats.landTiles * 100 << "% of land)\n";
    std::cout << "Mountain tiles: " << map->stats.mountainTiles 
              << " (" << (float)map->stats.mountainTiles / map->stats.landTiles * 100 << "% of land)\n";
    std::cout << "River tiles: " << map->stats.riverTiles << "\n";
    std::cout << "Average height: " << map->stats.averageHeight << "\n";
    std::cout << "Min height: " << map->stats.minHeight << "\n";
    std::cout << "Max height: " << map->stats.maxHeight << "\n";
}

// 测试批量生成
void testBatchGeneration() {
    std::cout << "\n=== Testing Batch Generation ===\n";
    
    MapGenerator::MapGenerator generator;
    
    MapGenerator::MapConfig baseConfig;
    baseConfig.width = 256;
    baseConfig.height = 256;
    baseConfig.noiseScale = 80.0f;
    baseConfig.seaLevel = 0.4f;
    
    std::cout << "Generating batch of 10 maps...\n";
    auto start = std::chrono::high_resolution_clock::now();
    
    auto batch = generator.generateBatch(baseConfig, 10);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Generated " << batch.size() << " maps in " 
              << duration.count() << "ms (" 
              << duration.count() / batch.size() << "ms per map)\n";
    
    // 导出每个地图
    for (size_t i = 0; i < std::min(batch.size(), size_t(3)); ++i) {
        std::string filename = "batch_" + std::to_string(i) + ".ppm";
        generator.exportToPPM(*batch[i], filename, true, 1);
        std::cout << "Exported map " << i << " to " << filename << "\n";
    }
}

// // 测试WFC算法
// void testWFCAlgorithm() {
//     std::cout << "\n=== Testing WFC Algorithm Variations ===\n";
    
//     MapGenerator::MapGenerator generator;
    
//     MapGenerator::MapConfig config;
//     config.width = 128;
//     config.height = 128;
//     config.seed = 9999;
    
//     // 测试不同WFC参数
//     std::vector<std::pair<uint32_t, std::string>> wfcTests = {
//         {100, "wfc_100_iter"},
//         {500, "wfc_500_iter"},
//         {1000, "wfc_1000_iter"},
//         {5000, "wfc_5000_iter"}
//     };
    
//     // for (const auto& test : wfcTests) {
//     //     config.wfcIterations = test.first;
        
//     //     std::cout << "Testing WFC with " << test.first << " iterations...\n";
//     //     auto map = generator.generateMap(config);
        
//     //     std::string filename = test.second + ".ppm";
//     //     generator.exportToPPM(*map, filename, true, 2);
//     //     std::cout << "Exported to " << filename << "\n";
//     // }
// }

// 测试噪声算法
void testNoiseAlgorithms() {
    std::cout << "\n=== Testing Noise Algorithm Performance ===\n";
    
    // 这个测试需要在NoiseGenerator中暴露更多接口
    // 这里只是演示思路
    MapGenerator::MapGenerator generator;
    
    MapGenerator::MapConfig config;
    config.width = 512;
    config.height = 512;
    
    // 测试不同尺寸的性能
    std::vector<std::pair<std::pair<uint32_t, uint32_t>, std::string>> sizes = {
        {{64, 64}, "tiny"},
        {{256, 256}, "small"},
        {{512, 512}, "medium"},
        {{1024, 1024}, "large"},
        {{2048, 2048}, "huge"}
    };
    
    for (const auto& size : sizes) {
        config.width = size.first.first;
        config.height = size.first.second;
        
        std::cout << "Generating " << size.second << " map (" 
                  << config.width << "x" << config.height << ")...\n";
        
        auto start = std::chrono::high_resolution_clock::now();
        auto map = generator.generateMap(config);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "Time: " << duration.count() << "ms\n";
        
        if (config.width <= 512 && config.height <= 512) {
            std::string filename = "noise_" + size.second + ".ppm";
            generator.exportToPPM(*map, filename, true, 0);
        }
    }
}

void testAdvancedExport() {
    std::cout << "\n=== Testing Advanced Export Functions ===\n";
    
    MapGenerator::MapGenerator generator;
    
    MapGenerator::MapConfig config;
    config.width = 256;
    config.height = 256;
    config.seed = 777;
    
    auto map = generator.generateMap(config);
    
    // 1. 导出伪彩色高度图
    {
        // 自定义渐变
        std::vector<MapGenerator::MapGenerator::Color> customGradient = {
            {0, 0, 100},      // 深蓝
            {0, 100, 200},    // 浅蓝
            {255, 255, 200},  // 沙滩
            {50, 150, 50},    // 绿
            {100, 100, 50},   // 黄绿
            {150, 100, 50},   // 棕
            {200, 200, 200},  // 灰
            {255, 255, 255}   // 白
        };
        
        generator.exportHeightmapToPPM(*map, "advanced_gradient.ppm", customGradient);
        std::cout << "Exported custom gradient heightmap\n";
    }
    
    // 2. 导出指定范围的高度图
    {
        float minHeight = map->stats.minHeight;
        float maxHeight = map->stats.maxHeight;
        generator.exportHeightmapToPGM(*map, "advanced_range.pgm", minHeight, maxHeight);
        std::cout << "Exported heightmap with custom range [" 
                  << minHeight << ", " << maxHeight << "]\n";
    }
    
    // 3. 导出地形类型索引图
    {
        generator.exportTerrainIndexToPGM(*map, "terrain_index.pgm");
        std::cout << "Exported terrain type index map\n";
    }
    
    // 4. 导出装饰类型索引图
    {
        // 创建装饰索引图
        std::vector<uint8_t> decorationData(map->config.width * map->config.height);
        uint32_t maxType = 0;
        for (uint32_t value : map->decorationMap) {
            maxType = std::max(maxType, value);
        }
        
        for (size_t i = 0; i < map->decorationMap.size(); ++i) {
            uint32_t type = map->decorationMap[i];
            decorationData[i] = static_cast<uint8_t>((type * 255) / (maxType ? maxType : 1));
        }
        
        // 保存为PGM
        std::ofstream file("decoration_index.pgm", std::ios::binary);
        file << "P5\n" << map->config.width << " " << map->config.height << "\n255\n";
        file.write(reinterpret_cast<const char*>(decorationData.data()), decorationData.size());
        std::cout << "Exported decoration type index map\n";
    }
}

void testMultipleExportFormats() {
    std::cout << "\n=== Testing Multiple Export Formats ===\n";
    
    MapGenerator::MapGenerator generator;
    
    // 生成一个小地图用于测试
    MapGenerator::MapConfig config;
    config.width = 64;
    config.height = 64;
    config.seed = 888;
    
    auto map = generator.generateMap(config);
    
    // 导出所有支持的格式
    generator.exportToPPM(*map, "test_all_formats_color.ppm", true, 0);
    generator.exportToPPM(*map, "test_all_formats_bw.ppm", false, 0);
    generator.exportToPGM(*map, "test_all_formats_gray.pgm");
    generator.exportHeightmapToPGM(*map, "test_all_formats_scaled.pgm", 0.2f, 0.8f);
    generator.exportTerrainIndexToPGM(*map, "test_all_formats_terrain_idx.pgm");
    
    std::cout << "Exported 5 different format variations\n";
    
    // 查看文件大小
    std::cout << "\nFile sizes:\n";
    auto printFileSize = [](const std::string& filename) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            std::streamsize size = file.tellg();
            std::cout << "  " << filename << ": " << size << " bytes ("
                      << (size / 1024.0) << " KB)\n";
        }
    };
    
    printFileSize("test_all_formats_color.ppm");
    printFileSize("test_all_formats_bw.ppm");
    printFileSize("test_all_formats_gray.pgm");
    printFileSize("test_all_formats_scaled.pgm");
    printFileSize("test_all_formats_terrain_idx.pgm");
}

// 生成示例地图用于文档
void generateExampleMapsForDocumentation() {
    std::cout << "\n=== Generating Example Maps for Documentation ===\n";
    
    MapGenerator::MapGenerator generator;
    
    // 1. 简单的小地图
    {
        MapGenerator::MapConfig config;
        config.width = 64;
        config.height = 64;
        config.seed = 1;
        config.noiseScale = 30.0f;
        config.seaLevel = 0.3f;
        
        auto map = generator.generateMap(config);
        generator.exportToPPM(*map, "doc_example_small.ppm", true, 1);
        generator.exportToPGM(*map, "doc_example_small_height.pgm");
        
        std::cout << "Generated small example map\n";
    }
    
    // 2. 中等尺寸，展示河流
    {
        MapGenerator::MapConfig config;
        config.width = 256;
        config.height = 256;
        config.seed = 2;
        config.noiseScale = 80.0f;
        config.seaLevel = 0.35f;
        config.plainHeight = 0.5f;
        
        auto map = generator.generateMap(config);
        generator.exportToPPM(*map, "doc_example_rivers.ppm", true, 1);
        
        std::cout << "Generated river example map\n";
    }
    
    // 3. 展示不同气候
    {
        std::vector<std::pair<MapGenerator::ClimateType, std::string>> climates = {
            {MapGenerator::ClimateType::TEMPERATE, "temperate"},
            {MapGenerator::ClimateType::TROPICAL, "tropical"},
            {MapGenerator::ClimateType::ARID, "arid"},
            {MapGenerator::ClimateType::POLAR, "polar"}
        };
        
        MapGenerator::MapConfig baseConfig;
        baseConfig.width = 256;
        baseConfig.height = 256;
        baseConfig.seed = 3;
        
        for (const auto& climate : climates) {
            baseConfig.climate = climate.first;
            if (climate.first == MapGenerator::ClimateType::ARID) {
                baseConfig.temperature = 0.9f;
                baseConfig.humidity = 0.1f;
            } else if (climate.first == MapGenerator::ClimateType::POLAR) {
                baseConfig.temperature = 0.2f;
                baseConfig.humidity = 0.4f;
            } else {
                baseConfig.temperature = 0.6f;
                baseConfig.humidity = 0.5f;
            }
            
            auto map = generator.generateMap(baseConfig);
            std::string filename = "doc_climate_" + climate.second + ".ppm";
            generator.exportToPPM(*map, filename, true, 1);
            
            std::cout << "Generated " << climate.second << " climate map\n";
        }
    }
}

// 导出原始数据用于其他分析
void exportRawData() {
    std::cout << "\n=== Exporting Raw Map Data ===\n";
    
    MapGenerator::MapGenerator generator;
    
    MapGenerator::MapConfig config;
    config.width = 128;
    config.height = 128;
    config.seed = 42;
    
    auto map = generator.generateMap(config);
    
    // 导出原始高度数据
    std::ofstream heightFile("raw_height_data.bin", std::ios::binary);
    heightFile.write(reinterpret_cast<const char*>(map->heightMap.data()), 
                     map->heightMap.size() * sizeof(float));
    heightFile.close();
    
    // 导出地形类型数据
    std::ofstream terrainFile("raw_terrain_data.bin", std::ios::binary);
    terrainFile.write(reinterpret_cast<const char*>(map->terrainMap.data()), 
                      map->terrainMap.size() * sizeof(uint32_t));
    terrainFile.close();
    
    // 导出元数据
    std::ofstream metaFile("raw_metadata.txt");
    metaFile << "Map Metadata\n";
    metaFile << "============\n";
    metaFile << "Width: " << map->config.width << "\n";
    metaFile << "Height: " << map->config.height << "\n";
    metaFile << "Seed: " << map->config.seed << "\n";
    metaFile << "Sea Level: " << map->config.seaLevel << "\n";
    metaFile << "Generation Time: " << map->generationTimeMs << "ms\n";
    metaFile << "Average Height: " << map->stats.averageHeight << "\n";
    metaFile << "Min Height: " << map->stats.minHeight << "\n";
    metaFile << "Max Height: " << map->stats.maxHeight << "\n";
    metaFile.close();
    
    std::cout << "Exported raw data files:\n";
    std::cout << "  - raw_height_data.bin (height map)\n";
    std::cout << "  - raw_terrain_data.bin (terrain types)\n";
    std::cout << "  - raw_metadata.txt (map metadata)\n";
}

// 演示命令行使用
void demonstrateCommandLineUsage() {
    std::cout << "\n=== Command Line Usage Example ===\n";
    std::cout << "\nExample: Generate a map and export images\n";
    std::cout << "========================================\n";
    
    MapGenerator::MapGenerator generator;
    
    // 从命令行参数读取配置（这里用硬编码模拟）
    uint32_t width = 512;
    uint32_t height = 512;
    uint32_t seed = 12345;
    std::string outputPrefix = "output";
    
    std::cout << "Parameters:\n";
    std::cout << "  Size: " << width << "x" << height << "\n";
    std::cout << "  Seed: " << seed << "\n";
    std::cout << "  Output prefix: " << outputPrefix << "\n";
    
    MapGenerator::MapConfig config;
    config.width = width;
    config.height = height;
    config.seed = seed;
    
    auto map = generator.generateMap(config);
    
    // 导出多种格式
    generator.exportToPPM(*map, outputPrefix + "_color.ppm", true, 1);
    generator.exportToPGM(*map, outputPrefix + "_height.pgm");
    
    // 生成缩略图
    MapGenerator::MapConfig thumbnailConfig = config;
    thumbnailConfig.width = 128;
    thumbnailConfig.height = 128;
    auto thumbnail = generator.generateMap(thumbnailConfig);
    generator.exportToPPM(*thumbnail, outputPrefix + "_thumbnail.ppm", true, 1);
    
    std::cout << "\nGenerated files:\n";
    std::cout << "  " << outputPrefix << "_color.ppm (full color map)\n";
    std::cout << "  " << outputPrefix << "_height.pgm (height map)\n";
    std::cout << "  " << outputPrefix << "_thumbnail.ppm (128x128 preview)\n";
    
    std::cout << "\nMap statistics:\n";
    std::cout << "  Water coverage: " 
              << (float)map->stats.waterTiles / (width * height) * 100 << "%\n";
    std::cout << "  Forest coverage: " 
              << (float)map->stats.forestTiles / map->stats.landTiles * 100 << "% of land\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   Map Generator Library - Examples\n";
    std::cout << "========================================\n\n";
    
    try {
        // 测试不同功能
        testPresets();
        testCustomConfig();
        testBatchGeneration();
        // testWFCAlgorithm();
        testNoiseAlgorithms();
        testAdvancedExport();
        testMultipleExportFormats();
        generateExampleMapsForDocumentation();
        exportRawData();
        demonstrateCommandLineUsage();
        
        std::cout << "\n========================================\n";
        std::cout << "   All examples completed successfully!\n";
        std::cout << "========================================\n";
        
        std::cout << "\nGenerated files in current directory:\n";
        std::cout << "  - Multiple .ppm files (color maps)\n";
        std::cout << "  - Multiple .pgm files (height maps)\n";
        std::cout << "  - Raw data files for analysis\n";
        std::cout << "  - Documentation examples\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

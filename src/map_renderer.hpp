#ifndef MAP_RENDERER_HPP
#define MAP_RENDERER_HPP

#include "map_reader.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>

class MapRenderer {
public:
    struct RenderMeta {
        double minLon, minLat, maxLon, maxLat;
        double scaleLon, scaleLat;
        double degLonPerTile, degLatPerTile;
        int textureRes;
        int rows, cols;
    };

    // Pre-render all tiles using multiple threads
    static void generateAllTiles(MapData& data, int textureRes, double kmPerTile, const std::string& outputDir, int numThreads = 13);

private:
    static void renderTileRange(const MapData& data, const RenderMeta& meta, 
                               int startRow, int endRow, const std::string& outputDir);
};

#endif

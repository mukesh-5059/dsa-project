#ifndef MAP_RENDERER_HPP
#define MAP_RENDERER_HPP

#include "map_reader.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>

struct renderer_pair_hash {
    inline std::size_t operator()(const std::pair<int, int>& v) const {
        return v.first * 100000 + v.second;
    }
};

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
    static void generateAllTiles(const MapData& data, int textureRes, double kmPerTile, const std::string& outputDir, int numThreads = 13);

private:
    static void renderTileRange(const MapData& data, const RenderMeta& meta, 
                               const std::unordered_map<std::pair<int, int>, std::vector<const WayData*>, renderer_pair_hash>& buckets,
                               int startRow, int endRow, const std::string& outputDir);
};

#endif

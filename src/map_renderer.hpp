#ifndef MAP_RENDERER_HPP
#define MAP_RENDERER_HPP

#include "map_reader.hpp"
#include <string>

class MapRenderer {
public:
    // textureRes: pixels (e.g. 1024)
    // kmPerTile: physical size (e.g. 1.0)
    static void generateTiles(const MapData& data, int textureRes, double kmPerTile, const std::string& outputDir);
};

#endif

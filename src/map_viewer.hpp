#ifndef MAP_VIEWER_HPP
#define MAP_VIEWER_HPP

#include <raylib.h>
#include <string>
#include <vector>
#include <unordered_map>
#include "map_reader.hpp"

struct MapMetadata {
    double minLon, minLat, maxLon, maxLat;
    double scaleLon, scaleLat;
    int textureRes;
    int rows, cols;
    double degLonPerTile, degLatPerTile;
};

struct MapLabel {
    std::string name;
    Vector2 worldPos;
    Color color;
    int adminLevel;
};

// Hash for unordered_map of pairs
struct viewer_pair_hash {
    inline std::size_t operator()(const std::pair<int, int> & v) const {
        return v.first * 100000 + v.second;
    }
};

class MapViewer {
public:
    MapViewer(const std::string& tilesDir, MapData& data, int res, double km);
    ~MapViewer();

    void run();

private:
    void initMetadata(int res, double km);
    void buildLabels();
    void handleInput();
    void updateVisibleTiles();
    void draw();
    void drawLegend();

    std::string m_tilesDir;
    MapData& m_mapData;
    MapMetadata m_meta;
    Camera2D m_camera;
    
    bool m_showLegend = false;
    std::vector<MapLabel> m_labels;
    
    // VRAM Cache
    std::unordered_map<std::pair<int, int>, Texture2D, viewer_pair_hash> m_tileCache;
};

#endif

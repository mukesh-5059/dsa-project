#ifndef MAP_VIEWER_HPP
#define MAP_VIEWER_HPP

#include <raylib.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include "map_reader.hpp"

struct MapMetadata {
    double minLon, minLat, maxLon, maxLat;
    double scaleLon, scaleLat;
    int textureRes;
    int rows, cols;
    double degLonPerTile, degLatPerTile;
};

// Hash for unordered_map of pairs
struct pair_hash {
    inline std::size_t operator()(const std::pair<int, int> & v) const {
        return v.first * 100000 + v.second;
    }
};

class MapViewer {
public:
    MapViewer(const std::string& tilesDir, MapData& data, int res, double km, std::atomic<bool>& stop_flag);
    ~MapViewer();

    void run();

private:
    void initMetadata(int res, double km);
    void buildSpatialIndex();
    void handleInput();
    void updateVisibleTiles();
    void draw();

    // Thread pool functions
    void workerThread();
    void enqueueTileGeneration(int r, int c);

    std::string m_tilesDir;
    MapData& m_mapData;
    MapMetadata m_meta;
    Camera2D m_camera;
    std::atomic<bool>& m_stopFlag;
    
    // Buckets: {row, col} -> list of ways in that tile
    std::unordered_map<std::pair<int, int>, std::vector<const WayData*>, pair_hash> m_buckets;
    
    // VRAM Cache
    std::unordered_map<std::pair<int, int>, Texture2D, pair_hash> m_tileCache;

    // --- Thread Pool ---
    std::vector<std::thread> m_workers;
    std::queue<std::pair<int, int>> m_taskQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_threadsRunning{true};
    std::unordered_map<std::pair<int, int>, bool, pair_hash> m_processingTiles; // Track what's being generated
};

#endif

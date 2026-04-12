#include "map_reader.hpp"
#include "map_viewer.hpp"
#include "map_renderer.hpp"
#include "path_finding.hpp"
#include <iostream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

int main() {
    std::string pbfPath = "map_data/chennai.osm.pbf";
    std::string tilesDir = "map_data/tiles";
    int textureRes = 4096;      
    double tileSizeKm = 5.0;    

    std::cout << "--- Map Loading ---" << std::endl;
    
    std::cout << "Clean the cache? (y/n): ";
    std::string answer;
    std::cin >> answer;
    
    if (answer == "y" || answer == "Y") {
        std::cout << "Cleaning cache: " << tilesDir << "...\n";
        if (fs::exists(tilesDir)) {
            fs::remove_all(tilesDir);
        }
        std::cout << "Cache cleaned.\n";
    }

    MapData mapData;
    std::cout << "Loading OSM data from " << pbfPath << "..." << std::endl;
    mapData.loadFromPbf(pbfPath);
    
    std::cout << "Building spatial index (buckets)..." << std::endl;
    mapData.buildBuckets(tileSizeKm);
    
    std::cout << "Building adjacency list for pathfinding..." << std::endl;
    mapData.makeAdjacencyList();

    if (!fs::exists(tilesDir) || fs::is_empty(tilesDir)) {
        std::cout << "Cache not found or empty...\n";
        MapRenderer::generateAllTiles(mapData, textureRes, tileSizeKm, tilesDir, 13);
    } else {
        std::cout << "Cache found. Skipping tile generation.\n";
    }

    std::cout << "---- Map Viewer ----" << std::endl;
    {
        MapViewer viewer(tilesDir, mapData, textureRes, tileSizeKm);
        viewer.run();
    }

    std::cout << "Cleaning up memory..." << std::endl;
    // The "Swap Trick": clear() only resets size, swap() actually deallocates the capacity back to the OS.
    std::unordered_map<long long, NodeData>().swap(mapData.nodes);
    std::vector<WayData>().swap(mapData.ways);
    std::vector<PlaceData>().swap(mapData.places);
    std::cout << "Cleanup complete." << std::endl;

    return 0;
}

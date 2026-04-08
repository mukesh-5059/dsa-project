#include "map_reader.hpp"
#include "map_viewer.hpp"
#include <iostream>
#include <boost/program_options.hpp>
#include <filesystem>
#include <csignal>
#include <atomic>

namespace po = boost::program_options;
namespace fs = std::filesystem;

std::atomic<bool> stop_flag{false};

void signal_handler(int signal) {
    std::cout << "\n[SIGINT] Interrupt received. shutingdown...\n";
    stop_flag = true;
}

int main(int argc, char* argv[]) {
    // Register signal handler for shutdown
    std::signal(SIGINT, signal_handler);

    po::options_description desc("Options");
    desc.add_options()
        ("clean", "delete the existing tile cache to start over")
        ("pbf", po::value<std::string>()->default_value("map_data/chennai_city.osm.pbf"), "path to OSM PBF file");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    // --- Core Parameters ---
    int textureRes = 4096;      
    double tileSizeKm = 5.0;    
    std::string pbfPath = vm["pbf"].as<std::string>();
    std::string tilesDir = "map_data/tiles";

    std::cout << "--- Map Loading ---" << std::endl;

    if (vm.count("clean")) {
        std::cout << "Cleaning cache directory: " << tilesDir << "...\n";
        if (fs::exists(tilesDir)) {
            fs::remove_all(tilesDir);
        }
        std::cout << "Cache cleaned.\n";
    }

    MapData mapData;
    std::cout << "Loading OSM data from " << pbfPath << "..." << std::endl;
    mapData.loadFromPbf(pbfPath);

    if (stop_flag) goto cleanup;

    std::cout << "---- Map Viewer ----" << std::endl;
    {
        MapViewer viewer(tilesDir, mapData, textureRes, tileSizeKm, stop_flag);
        viewer.run();
    }

cleanup:
    std::cout << "Cleaning up memory..." << std::endl;
    // The "Swap Trick": clear() only resets size, swap() actually deallocates the capacity back to the OS.
    std::unordered_map<long long, NodeData>().swap(mapData.nodes);
    std::vector<WayData>().swap(mapData.ways);
    std::vector<PlaceData>().swap(mapData.places);
    std::cout << "Cleanup complete." << std::endl;

    return 0;
}

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
    std::cout << "\n[SIGINT] Interrupt received. Initiating graceful shutdown...\n";
    stop_flag = true;
}

int main(int argc, char* argv[]) {
    // Register signal handler for graceful shutdown
    std::signal(SIGINT, signal_handler);

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "produce help message")
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

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 0;
    }

    // --- Core Parameters ---
    int textureRes = 4096;
    double tileSizeKm = 10.0;
    std::string pbfPath = vm["pbf"].as<std::string>();
    std::string tilesDir = "map_data/tiles";

    std::cout << "--- JIT MAP ENGINE ---" << std::endl;

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

    std::cout << "Launching Interactive Viewer..." << std::endl;
    {
        MapViewer viewer(tilesDir, mapData, textureRes, tileSizeKm, stop_flag);
        viewer.run();
    }

cleanup:
    std::cout << "Cleaning up memory..." << std::endl;
    mapData.nodes.clear();
    mapData.ways.clear();
    std::cout << "Cleanup complete. Goodbye!" << std::endl;

    return 0;
}

#include "map_reader.hpp"
#include "map_renderer.hpp"
#include <iostream>
#include <boost/program_options.hpp>
#include <filesystem>

namespace po = boost::program_options;
namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help,h", "produce help message")
        ("rebuild-cache", "re-parse OSM data and recreate tiles")
        ("res,r", po::value<int>()->default_value(1024), "resolution of each tile in pixels")
        ("tile-size,s", po::value<double>()->default_value(1.0), "physical size of each tile in km")
        ("render", "start the interactive map viewer");

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

    std::string pbfPath = "map_data/map_small.osm.pbf";
    std::string tilesDir = "map_data/tiles";

    if (vm.count("rebuild-cache")) {
        std::cout << "Rebuilding cache...\n";
        if (fs::exists(tilesDir)) {
            fs::remove_all(tilesDir);
        }
        
        MapData mapData;
        std::cout << "Loading OSM data from " << pbfPath << "..." << std::endl;
        mapData.loadFromPbf(pbfPath);

        int res = vm["res"].as<int>();
        double tileSize = vm["tile-size"].as<double>();
        
        std::cout << "Generating raster tiles in " << tilesDir << "..." << std::endl;
        MapRenderer::generateTiles(mapData, res, tileSize, tilesDir);
        std::cout << "Cache rebuilt successfully.\n";
    }

    if (vm.count("render")) {
        if (!fs::exists(tilesDir + "/metadata.txt")) {
            std::cerr << "Error: Cache not found. Please run with --rebuild-cache first.\n";
            return 1;
        }

        std::cout << "Starting renderer...\n";
        // TODO: Implement Phase 2: Runtime Renderer
        // For now, just a placeholder
        std::cout << "Phase 2 implementation coming soon!\n";
    }

    if (!vm.count("rebuild-cache") && !vm.count("render")) {
        std::cout << "No action specified. Use --help for options.\n";
    }

    return 0;
}

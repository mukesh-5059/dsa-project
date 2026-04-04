#include "map_renderer.hpp"
#include <raylib.h>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

void MapRenderer::generateTiles(const MapData& data, int textureRes, double kmPerTile, const std::string& outputDir) {
    // 1. Initialize Raylib in hidden mode for offscreen rendering
    InitWindow(100, 100, "Tile Generator");
    SetTargetFPS(60);

    if (!fs::exists(outputDir)) {
        fs::create_directories(outputDir);
    }

    // 2. Define map bounds
    double minLon = data.bounds.min_lon;
    double minLat = data.bounds.min_lat;
    double maxLon = data.bounds.max_lon;
    double maxLat = data.bounds.max_lat;

    // 3. Convert KM per tile to Degrees per tile
    // 1 degree latitude = ~111.32 km
    double degLatPerTile = kmPerTile / 111.32;
    // 1 degree longitude = ~111.32 * cos(latitude) km
    double centerLat = (minLat + maxLat) / 2.0;
    double degLonPerTile = kmPerTile / (111.32 * std::cos(centerLat * M_PI / 180.0));

    // 4. Calculate how many tiles we need to cover the bounding box
    int cols = std::ceil((maxLon - minLon) / degLonPerTile);
    int rows = std::ceil((maxLat - minLat) / degLatPerTile);

    // Calculate the pixel scale for rendering (pixels per degree)
    double scaleLon = textureRes / degLonPerTile;
    double scaleLat = textureRes / degLatPerTile;

    std::cout << "Target: " << kmPerTile << "km per tile at " << textureRes << "px resolution.\n";
    std::cout << "Grid: " << cols << "x" << rows << " tiles (" << cols * rows << " total).\n";

    RenderTexture2D target = LoadRenderTexture(textureRes, textureRes);

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            BeginTextureMode(target);
            ClearBackground(RAYWHITE);

            // Calculate the bounds of this specific tile in Degrees
            double tileMinLon = minLon + (c * degLonPerTile);
            double tileMaxLon = minLon + ((c + 1) * degLonPerTile);
            double tileMinLat = maxLat - ((r + 1) * degLatPerTile); // Y is inverted in screen space
            double tileMaxLat = maxLat - (r * degLatPerTile);

            // Draw Ways
            for (const auto& way : data.ways) {
                Color wayColor = DARKGRAY;
                float thickness = 2.0f;

                if (way.tags.count("highway")) {
                    wayColor = GRAY;
                    thickness = 3.0f;
                } else if (way.tags.count("building")) {
                    wayColor = LIGHTGRAY;
                    thickness = 1.0f;
                }

                for (size_t i = 0; i < way.node_ids.size() - 1; i++) {
                    if (data.nodes.count(way.node_ids[i]) && data.nodes.count(way.node_ids[i+1])) {
                        const auto& n1 = data.nodes.at(way.node_ids[i]);
                        const auto& n2 = data.nodes.at(way.node_ids[i+1]);

                        // Skip if both nodes are outside this tile's bounds (rough optimization)
                        if ((n1.lon < tileMinLon && n2.lon < tileMinLon) || 
                            (n1.lon > tileMaxLon && n2.lon > tileMaxLon) ||
                            (n1.lat < tileMinLat && n2.lat < tileMinLat) || 
                            (n1.lat > tileMaxLat && n2.lat > tileMaxLat)) continue;

                        // Map lat/lon to tile pixels using separate scales for X and Y to keep 1km square
                        float x1 = (n1.lon - tileMinLon) * scaleLon;
                        float y1 = (tileMaxLat - n1.lat) * scaleLat;
                        float x2 = (n2.lon - tileMinLon) * scaleLon;
                        float y2 = (tileMaxLat - n2.lat) * scaleLat;

                        DrawLineEx({x1, y1}, {x2, y2}, thickness, wayColor);
                    }
                }
            }
            EndTextureMode();

            // Save to disk
            Image image = LoadImageFromTexture(target.texture);
            ImageFlipVertical(&image);
            std::string filename = outputDir + "/tile_" + std::to_string(r) + "_" + std::to_string(c) + ".png";
            ExportImage(image, filename.c_str());
            UnloadImage(image);
        }
    }

    UnloadRenderTexture(target);

    // 5. Save Updated Metadata
    std::ofstream meta(outputDir + "/metadata.txt");
    if (meta.is_open()) {
        meta << minLon << " " << minLat << " " << maxLon << " " << maxLat << "\n";
        meta << scaleLon << " " << scaleLat << "\n"; // Save both scales
        meta << textureRes << "\n";
        meta << rows << " " << cols << "\n";
        meta << degLonPerTile << " " << degLatPerTile << "\n"; // Also save physical degree size
        meta.close();
    }

    CloseWindow();
}

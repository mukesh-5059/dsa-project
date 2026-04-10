#include "map_renderer.hpp"
#include <raylib.h>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <thread>

namespace fs = std::filesystem;

int safeStoi(const std::string& str, int defaultVal = 1) {
    try { return std::stoi(str); } catch (...) { return defaultVal; }
}

void fillPolygon(Image* dst, const std::vector<Vector2>& points, Color color) {
    if (points.size() < 3) return;
    int minX = dst->width, maxX = 0, minY = dst->height, maxY = 0;
    for (const auto& p : points) {
        minX = std::min(minX, (int)p.x);
        maxX = std::max(maxX, (int)p.x);
        minY = std::min(minY, (int)p.y);
        maxY = std::max(maxY, (int)p.y);
    }
    minY = std::max(0, minY); maxY = std::min(dst->height - 1, maxY);
    
    std::vector<int> nodes;
    nodes.reserve(32); 

    for (int y = minY; y <= maxY; y++) {
        nodes.clear(); 
        size_t j = points.size() - 1;
        for (size_t i = 0; i < points.size(); i++) {
            if ((points[i].y < (float)y && points[j].y >= (float)y) || (points[j].y < (float)y && points[i].y >= (float)y)) {
                if (std::abs(points[j].y - points[i].y) > 0.0001f)
                    nodes.push_back((int)(points[i].x + (y - points[i].y) / (points[j].y - points[i].y) * (points[j].x - points[i].x)));
            }
            j = i;
        }
        std::sort(nodes.begin(), nodes.end());
        for (size_t i = 0; i < nodes.size(); i += 2) {
            if (i + 1 >= nodes.size()) break;
            int startX = std::max(0, nodes[i]); int endX = std::min(dst->width - 1, nodes[i+1]);
            for (int x = startX; x <= endX; x++) ImageDrawPixel(dst, x, y, color);
        }
    }
}

void MapRenderer::generateAllTiles(const MapData& data, int textureRes, double kmPerTile, const std::string& outputDir, int numThreads) {
    if (!fs::exists(outputDir)) fs::create_directories(outputDir);

    RenderMeta meta;
    meta.textureRes = textureRes;
    meta.minLon = data.bounds.min_lon; meta.minLat = data.bounds.min_lat;
    meta.maxLon = data.bounds.max_lon; meta.maxLat = data.bounds.max_lat;
    meta.degLatPerTile = kmPerTile / 111.32;
    double centerLat = (meta.minLat + meta.maxLat) / 2.0;
    meta.degLonPerTile = kmPerTile / (111.32 * std::cos(centerLat * M_PI / 180.0));
    meta.cols = std::ceil((meta.maxLon - meta.minLon) / meta.degLonPerTile);
    meta.rows = std::ceil((meta.maxLat - meta.minLat) / meta.degLatPerTile);
    meta.scaleLon = textureRes / meta.degLonPerTile; 
    meta.scaleLat = textureRes / meta.degLatPerTile;

    std::cout << "Target: " << kmPerTile << "km per tile at " << textureRes << "px resolution.\n";
    std::cout << "Grid: " << meta.cols << "x" << meta.rows << " tiles (" << meta.cols * meta.rows << " total).\n";
    std::cout << "Indexing ways for parallel rendering...\n";

    std::unordered_map<std::pair<int, int>, std::vector<const WayData*>, renderer_pair_hash> buckets;
    for (const auto& way : data.ways) {
        double wMinLon = 180, wMaxLon = -180, wMinLat = 90, wMaxLat = -90;
        bool hasNodes = false;
        for (long long id : way.node_ids) {
            if (data.nodes.count(id)) {
                const auto& n = data.nodes.at(id);
                wMinLon = std::min(wMinLon, n.lon); wMaxLon = std::max(wMaxLon, n.lon);
                wMinLat = std::min(wMinLat, n.lat); wMaxLat = std::max(wMaxLat, n.lat);
                hasNodes = true;
            }
        }
        if (!hasNodes) continue;
        int cStart = std::max(0, (int)floor((wMinLon - meta.minLon) / meta.degLonPerTile));
        int cEnd   = std::min(meta.cols - 1, (int)floor((wMaxLon - meta.minLon) / meta.degLonPerTile));
        int rStart = std::max(0, (int)floor((meta.maxLat - wMaxLat) / meta.degLatPerTile));
        int rEnd   = std::min(meta.rows - 1, (int)floor((meta.maxLat - wMinLat) / meta.degLatPerTile));
        for (int r = rStart; r <= rEnd; r++) {
            for (int c = cStart; c <= cEnd; c++) buckets[{r, c}].push_back(&way);
        }
    }

    std::cout << "Rendering tiles with " << numThreads << " threads...\n";

    std::vector<std::thread> workers;
    int rowsPerThread = std::ceil((double)meta.rows / numThreads);
    for (int i = 0; i < numThreads; i++) {
        int startRow = i * rowsPerThread;
        int endRow = std::min(startRow + rowsPerThread, meta.rows);
        if (startRow >= meta.rows) break;
        workers.emplace_back(&MapRenderer::renderTileRange, std::ref(data), std::ref(meta), std::ref(buckets), startRow, endRow, outputDir);
    }

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    std::cout << "All tiles generated successfully.\n";
}

void MapRenderer::renderTileRange(const MapData& data, const RenderMeta& meta, 
                                 const std::unordered_map<std::pair<int, int>, std::vector<const WayData*>, renderer_pair_hash>& buckets,
                                 int startRow, int endRow, const std::string& outputDir) {
    for (int r = startRow; r < endRow; r++) {
        for (int c = 0; c < meta.cols; c++) {
            if (!buckets.count({r, c})) continue;

            double tileMinLon = meta.minLon + (c * meta.degLonPerTile);
            double tileMaxLat = meta.maxLat - (r * meta.degLatPerTile);

            Image image = GenImageColor(meta.textureRes, meta.textureRes, { 5, 5, 10, 255 });
            
            for (const auto* wayPtr : buckets.at({r, c})) {
                const WayData& way = *wayPtr;
                Color fillColor = {0, 0, 0, 0};
                if (way.tags.count("natural") && way.tags.at("natural") == "water") fillColor = { 0, 80, 180, 255 };
                else if (way.tags.count("landuse")) {
                    std::string l = way.tags.at("landuse");
                    if (l == "grass" || l == "forest" || l == "park") fillColor = { 20, 80, 40, 255 };
                } else if (way.tags.count("building")) {
                    int levels = way.tags.count("building:levels") ? safeStoi(way.tags.at("building:levels"), 1) : 1;
                    unsigned char bright = std::min(255, 40 + levels * 20);
                    fillColor = { (unsigned char)(bright/2), (unsigned char)(bright/4), bright, 255 };
                }
                if (fillColor.a > 0 && way.node_ids.size() > 2 && way.node_ids.front() == way.node_ids.back()) {
                    std::vector<Vector2> poly;
                    for (long long id : way.node_ids) {
                        if (data.nodes.count(id)) {
                            const auto& n = data.nodes.at(id);
                            poly.push_back({ (float)((n.lon - tileMinLon) * meta.scaleLon), (float)((tileMaxLat - n.lat) * meta.scaleLat) });
                        }
                    }
                    fillPolygon(&image, poly, fillColor);
                }
            }
            
            for (const auto* wayPtr : buckets.at({r, c})) {
                const WayData& way = *wayPtr;
                Color wayColor = { 80, 80, 100, 255 }; 
                int thickness = 1;
                if (way.tags.count("highway")) {
                    std::string h = way.tags.at("highway");
                    if (h == "motorway" || h == "trunk") { wayColor = { 255, 80, 0, 255 }; thickness = 4; }
                    else if (h == "primary") { wayColor = { 255, 200, 0, 255 }; thickness = 3; }
                    else if (h == "secondary") { wayColor = { 0, 255, 180, 255 }; thickness = 2; }
                    else if (h == "tertiary") { wayColor = { 200, 200, 255, 255 }; thickness = 1; }
                    if (way.tags.count("access") && way.tags.at("access") == "private") wayColor = Fade(wayColor, 0.3f);
                } else if (way.tags.count("railway") || way.tags.count("power")) { wayColor = { 255, 50, 150, 255 }; thickness = 2; }
                else continue;
                for (size_t i = 0; i < way.node_ids.size() - 1; i++) {
                    if (data.nodes.count(way.node_ids[i]) && data.nodes.count(way.node_ids[i+1])) {
                        const auto& n1 = data.nodes.at(way.node_ids[i]); const auto& n2 = data.nodes.at(way.node_ids[i+1]);
                        float x1 = (float)((n1.lon - tileMinLon) * meta.scaleLon), y1 = (float)((tileMaxLat - n1.lat) * meta.scaleLat);
                        float x2 = (float)((n2.lon - tileMinLon) * meta.scaleLon), y2 = (float)((tileMaxLat - n2.lat) * meta.scaleLat);
                        if (thickness > 1) {
                            for (int dx = -thickness/2; dx <= thickness/2; dx++)
                                for (int dy = -thickness/2; dy <= thickness/2; dy++)
                                    ImageDrawLine(&image, (int)x1+dx, (int)y1+dy, (int)x2+dx, (int)y2+dy, wayColor);
                        } else ImageDrawLine(&image, (int)x1, (int)y1, (int)x2, (int)y2, wayColor);
                    }
                }
            }
            std::string path = outputDir + "/tile_" + std::to_string(r) + "_" + std::to_string(c) + ".png";
            ExportImage(image, path.c_str());
            UnloadImage(image);
        }
    }
}

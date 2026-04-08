#include "map_viewer.hpp"
#include <raymath.h>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <cmath>

namespace fs = std::filesystem;

int safeStoi(const std::string& str, int defaultVal = 1) {
    try { return std::stoi(str); } catch (...) { return defaultVal; }
}

void fillPolygon(Image* dst, const std::vector<Vector2>& points, Color color) {
    if (points.size() < 3) return;
    int minX = dst->width, maxX = 0, minY = dst->height, maxY = 0;
    for (const auto& p : points) {
        minX = std::min(minX, (int)p.x); maxX = std::max(maxX, (int)p.x);
        minY = std::min(minY, (int)p.y); maxY = std::max(maxY, (int)p.y);
    }
    minY = std::max(0, minY); maxY = std::min(dst->height - 1, maxY);
    
    // Reuse a vector to reduce allocations per scanline
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

MapViewer::MapViewer(const std::string& tilesDir, MapData& data, int res, double km, std::atomic<bool>& stop_flag) 
    : m_tilesDir(tilesDir), m_mapData(data), m_stopFlag(stop_flag) {
    initMetadata(res, km);
    buildSpatialIndex();
    float worldWidth = (float)(m_meta.cols * m_meta.textureRes);
    float worldHeight = (float)(m_meta.rows * m_meta.textureRes);
    m_camera.offset = { 1280 / 2.0f, 720 / 2.0f };
    m_camera.target = { worldWidth / 2.0f, worldHeight / 2.0f };
    m_camera.rotation = 0.0f;
    m_camera.zoom = 0.01f;
    unsigned int numThreads = std::thread::hardware_concurrency();
    if (numThreads > 3) numThreads -= 3; else numThreads = 1;
    m_threadsRunning = true;
    for (unsigned int i = 0; i < numThreads; ++i) m_workers.emplace_back(&MapViewer::workerThread, this);
}

MapViewer::~MapViewer() {
    m_threadsRunning = false; m_cv.notify_all();
    for (auto& t : m_workers) if (t.joinable()) t.join();
    for (auto& [pos, tex] : m_tileCache) UnloadTexture(tex);
}

void MapViewer::initMetadata(int res, double km) {
    m_meta.textureRes = res;
    m_meta.minLon = m_mapData.bounds.min_lon; m_meta.minLat = m_mapData.bounds.min_lat;
    m_meta.maxLon = m_mapData.bounds.max_lon; m_meta.maxLat = m_mapData.bounds.max_lat;
    m_meta.degLatPerTile = km / 111.32;
    double centerLat = (m_meta.minLat + m_meta.maxLat) / 2.0;
    m_meta.degLonPerTile = km / (111.32 * std::cos(centerLat * M_PI / 180.0));
    m_meta.cols = std::ceil((m_meta.maxLon - m_meta.minLon) / m_meta.degLonPerTile);
    m_meta.rows = std::ceil((m_meta.maxLat - m_meta.minLat) / m_meta.degLatPerTile);
    m_meta.scaleLon = res / m_meta.degLonPerTile; m_meta.scaleLat = res / m_meta.degLatPerTile;
    if (!fs::exists(m_tilesDir)) fs::create_directories(m_tilesDir);
}

void MapViewer::buildSpatialIndex() {
    std::cout << "Indexing ways and places..." << std::endl;
    // 1. Extract Place Labels
    for (const auto& p : m_mapData.places) {
        Vector2 pos = { (float)((p.lon - m_meta.minLon) * m_meta.scaleLon), (float)((m_meta.maxLat - p.lat) * m_meta.scaleLat) };
        Color c = WHITE;
        if (p.admin_level <= 4) c = GOLD;
        else if (p.admin_level <= 6) c = SKYBLUE;
        m_labels.push_back({ p.name, pos, c, p.admin_level });
    }

    // 2. Index Ways
    for (const auto& way : m_mapData.ways) {
        if (m_stopFlag) return;
        
        // Landmark Labels (POIs)
        if (way.tags.count("name") && (way.tags.count("amenity") || way.tags.count("shop") || way.tags.count("university"))) {
            long long midNode = way.node_ids[way.node_ids.size()/2];
            if (m_mapData.nodes.count(midNode)) {
                const auto& n = m_mapData.nodes.at(midNode);
                Vector2 pos = { (float)((n.lon - m_meta.minLon) * m_meta.scaleLon), (float)((m_meta.maxLat - n.lat) * m_meta.scaleLat) };
                m_labels.push_back({ way.tags.at("name"), pos, RAYWHITE, 12 }); // Level 12 for buildings/POIs
            }
        }

        double wMinLon = 180, wMaxLon = -180, wMinLat = 90, wMaxLat = -90;
        bool hasNodes = false;
        for (long long id : way.node_ids) {
            if (m_mapData.nodes.count(id)) {
                const auto& n = m_mapData.nodes.at(id);
                wMinLon = std::min(wMinLon, n.lon); wMaxLon = std::max(wMaxLon, n.lon);
                wMinLat = std::min(wMinLat, n.lat); wMaxLat = std::max(wMaxLat, n.lat);
                hasNodes = true;
            }
        }
        if (!hasNodes) continue;
        int cStart = std::max(0, (int)floor((wMinLon - m_meta.minLon) / m_meta.degLonPerTile));
        int cEnd   = std::min(m_meta.cols - 1, (int)floor((wMaxLon - m_meta.minLon) / m_meta.degLonPerTile));
        int rStart = std::max(0, (int)floor((m_meta.maxLat - wMaxLat) / m_meta.degLatPerTile));
        int rEnd   = std::min(m_meta.rows - 1, (int)floor((m_meta.maxLat - wMinLat) / m_meta.degLatPerTile));
        for (int r = rStart; r <= rEnd; r++) {
            for (int c = cStart; c <= cEnd; c++) m_buckets[{r, c}].push_back(&way);
        }
    }
}

void MapViewer::enqueueTileGeneration(int r, int c) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_processingTiles.count({r, c})) return;
    m_processingTiles[{r, c}] = true; m_taskQueue.push({r, c}); m_cv.notify_one();
}

void MapViewer::workerThread() {
    while (m_threadsRunning && !m_stopFlag) {
        std::pair<int, int> tilePos;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait(lock, [this]{ return !m_taskQueue.empty() || !m_threadsRunning || m_stopFlag; });
            if (!m_threadsRunning || m_stopFlag) return;
            tilePos = m_taskQueue.front(); m_taskQueue.pop();
        }
        int r = tilePos.first, c = tilePos.second;
        std::string path = m_tilesDir + "/tile_" + std::to_string(r) + "_" + std::to_string(c) + ".png";
        try {
            if (!fs::exists(path) && m_buckets.count({r, c})) {
                double tileMinLon = m_meta.minLon + (c * m_meta.degLonPerTile);
                double tileMaxLat = m_meta.maxLat - (r * m_meta.degLatPerTile);
                Image image = GenImageColor(m_meta.textureRes, m_meta.textureRes, { 5, 5, 10, 255 });
                for (const auto* wayPtr : m_buckets[{r, c}]) {
                    if (m_stopFlag || !m_threadsRunning) break;
                    const WayData& way = *wayPtr; Color fillColor = {0, 0, 0, 0};
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
                            if (m_mapData.nodes.count(id)) {
                                const auto& n = m_mapData.nodes.at(id);
                                poly.push_back({ (float)((n.lon - tileMinLon) * m_meta.scaleLon), (float)((tileMaxLat - n.lat) * m_meta.scaleLat) });
                            }
                        }
                        fillPolygon(&image, poly, fillColor);
                    }
                }
                for (const auto* wayPtr : m_buckets[{r, c}]) {
                    if (m_stopFlag || !m_threadsRunning) break;
                    const WayData& way = *wayPtr; Color wayColor = { 80, 80, 100, 255 }; int thickness = 1;
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
                        if (m_mapData.nodes.count(way.node_ids[i]) && m_mapData.nodes.count(way.node_ids[i+1])) {
                            const auto& n1 = m_mapData.nodes.at(way.node_ids[i]); const auto& n2 = m_mapData.nodes.at(way.node_ids[i+1]);
                            float x1 = (float)((n1.lon - tileMinLon) * m_meta.scaleLon), y1 = (float)((tileMaxLat - n1.lat) * m_meta.scaleLat);
                            float x2 = (float)((n2.lon - tileMinLon) * m_meta.scaleLon), y2 = (float)((tileMaxLat - n2.lat) * m_meta.scaleLat);
                            if (thickness > 1) {
                                for (int dx = -thickness/2; dx <= thickness/2; dx++)
                                    for (int dy = -thickness/2; dy <= thickness/2; dy++)
                                        ImageDrawLine(&image, (int)x1+dx, (int)y1+dy, (int)x2+dx, (int)y2+dy, wayColor);
                            } else ImageDrawLine(&image, (int)x1, (int)y1, (int)x2, (int)y2, wayColor);
                        }
                    }
                }
                if (m_threadsRunning && !m_stopFlag) ExportImage(image, path.c_str());
                UnloadImage(image);
            }
        } catch (...) { std::cerr << "Thread Error at " << r << "," << c << std::endl; }
        { std::lock_guard<std::mutex> lock(m_queueMutex); m_processingTiles.erase({r, c}); }
    }
}

void MapViewer::handleInput() {
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 delta = GetMouseDelta(); m_camera.target = Vector2Add(m_camera.target, Vector2Scale(delta, -1.0f / m_camera.zoom));
    }
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), m_camera);
        m_camera.offset = GetMousePosition(); m_camera.target = mouseWorldPos;
        m_camera.zoom += (wheel * 0.1f * m_camera.zoom);
        m_camera.zoom = std::clamp(m_camera.zoom, 0.0001f, 10.0f);
    }
    if (IsKeyPressed(KEY_L)) m_showLegend = !m_showLegend;
}

void MapViewer::updateVisibleTiles() {
    Vector2 topLeft = GetScreenToWorld2D({0, 0}, m_camera);
    Vector2 bottomRight = GetScreenToWorld2D({(float)GetScreenWidth(), (float)GetScreenHeight()}, m_camera);
    int startCol = floor(topLeft.x / m_meta.textureRes), endCol = ceil(bottomRight.x / m_meta.textureRes);
    int startRow = floor(topLeft.y / m_meta.textureRes), endRow = ceil(bottomRight.y / m_meta.textureRes);
    if (m_meta.textureRes * m_camera.zoom < 64.0f) {
        if (!m_tileCache.empty()) { for (auto& [pos, tex] : m_tileCache) UnloadTexture(tex); m_tileCache.clear(); }
        return;
    }
    std::vector<std::pair<int, int>> visible; int loaded = 0;
    for (int r = startRow; r <= endRow; r++) {
        for (int c = startCol; c <= endCol; c++) {
            if (r >= 0 && r < m_meta.rows && c >= 0 && c < m_meta.cols && m_buckets.count({r, c})) {
                visible.push_back({r, c});
                if (!m_tileCache.count({r, c})) {
                    std::string path = m_tilesDir + "/tile_" + std::to_string(r) + "_" + std::to_string(c) + ".png";
                    if (fs::exists(path)) {
                        if (loaded < 1 && m_tileCache.size() < 32) {
                            m_tileCache[{r, c}] = LoadTexture(path.c_str());
                            GenTextureMipmaps(&m_tileCache[{r, c}]); SetTextureFilter(m_tileCache[{r, c}], TEXTURE_FILTER_BILINEAR);
                            loaded++;
                        }
                    } else enqueueTileGeneration(r, c);
                }
            }
        }
    }
    if (m_tileCache.size() >= 32) {
        for (auto it = m_tileCache.begin(); it != m_tileCache.end();) {
            bool is_v = false; for (auto& v : visible) if (v == it->first) is_v = true;
            if (!is_v) { UnloadTexture(it->second); it = m_tileCache.erase(it); } else ++it;
        }
    }
}

void MapViewer::drawLegend() {
    DrawRectangle(GetScreenWidth() - 220, 10, 210, 200, Fade(BLACK, 0.8f));
    DrawRectangleLines(GetScreenWidth() - 220, 10, 210, 200, SKYBLUE);
    int y = 25;
    auto item = [&](std::string name, Color c) {
        DrawRectangle(GetScreenWidth() - 205, y, 15, 15, c);
        DrawText(name.c_str(), GetScreenWidth() - 180, y + 2, 10, WHITE); y += 20;
    };
    DrawText("MAP LEGEND (L to hide)", GetScreenWidth() - 205, y, 10, SKYBLUE); y += 25;
    item("Motorway / Trunk", { 255, 80, 0, 255 });
    item("Primary Road", { 255, 200, 0, 255 });
    item("Secondary Road", { 0, 255, 180, 255 });
    item("Railway / Power", { 255, 50, 150, 255 });
    item("Water Bodies", { 0, 80, 180, 255 });
    item("Parks / Grass", { 20, 80, 40, 255 });
    item("Buildings", { 80, 40, 120, 255 });
}

void MapViewer::draw() {
    BeginDrawing(); ClearBackground({5, 5, 10, 255});
    BeginMode2D(m_camera);
    for (int r = 0; r < m_meta.rows; r++) {
        for (int c = 0; c < m_meta.cols; c++) {
            DrawRectangleLines(c * m_meta.textureRes, r * m_meta.textureRes, m_meta.textureRes, m_meta.textureRes, Fade(DARKBLUE, 0.3f));
        }
    }
    for (auto& [pos, tex] : m_tileCache) DrawTexture(tex, pos.second * m_meta.textureRes, pos.first * m_meta.textureRes, WHITE);
    
    // --- LABEL DRAWING ---
    for (const auto& lb : m_labels) {
        bool show = false;
        float fontSize = 12.0f;
        
        if (lb.adminLevel <= 4) show = true; // Countries/States always
        else if (lb.adminLevel <= 6 && m_camera.zoom > 0.05f) show = true; // Cities
        else if (lb.adminLevel <= 8 && m_camera.zoom > 0.15f) show = true; // Towns/Suburbs
        else if (lb.adminLevel <= 10 && m_camera.zoom > 0.40f) show = true; // Neighborhoods
        else if (lb.adminLevel > 10 && m_camera.zoom > 0.60f) show = true; // POIs/Landmarks

        if (show) {
            float labelScale = 1.0f / m_camera.zoom;
            float finalFontSize = fontSize * labelScale * (lb.adminLevel <= 6 ? 2.0f : 1.0f);
            DrawCircleV(lb.worldPos, 4.0f * labelScale, lb.color);
            DrawTextEx(GetFontDefault(), lb.name.c_str(), {lb.worldPos.x + (6.0f * labelScale), lb.worldPos.y}, finalFontSize, 1.0f, WHITE);
        }
    }

    double homeLon = 80.084, homeLat = 12.919;
    float homeX = (float)((homeLon - m_meta.minLon) * m_meta.scaleLon), homeY = (float)((m_meta.maxLat - homeLat) * m_meta.scaleLat);
    float markerScale = 1.0f / m_camera.zoom, baseRadius = 8.0f * markerScale, pulse = (sin(GetTime() * 4.0f) + 1.0f) * (4.0f * markerScale);
    DrawCircleV({homeX, homeY}, baseRadius * 2.0f + pulse, Fade(LIME, 0.3f));
    DrawCircleV({homeX, homeY}, baseRadius, GREEN); DrawCircleV({homeX, homeY}, baseRadius * 0.5f, WHITE);
    DrawTextEx(GetFontDefault(), "MY HOME", {homeX + baseRadius, homeY - 20.0f * markerScale}, 20.0f * markerScale, 2.0f, GREEN);
    EndMode2D();

    DrawRectangle(10, 10, 300, 120, Fade(BLACK, 0.8f));
    DrawText("MAP VIEWER", 20, 20, 10, SKYBLUE);
    DrawText("Right Click: Pan | Wheel: Zoom", 20, 35, 10, WHITE);
    DrawText(TextFormat("Tiles in VRAM: %d / 32", (int)m_tileCache.size()), 20, 50, 10, GREEN);
    int queued = 0; { std::lock_guard<std::mutex> lock(m_queueMutex); queued = m_taskQueue.size(); }
    DrawText(TextFormat("Tiles Processing: %d", queued), 20, 65, 10, (queued > 0 ? ORANGE : GREEN));
    DrawText(TextFormat("Zoom: %.4f | 'L' for Legend", m_camera.zoom), 20, 80, 10, WHITE);
    DrawText(TextFormat("FPS: %d", GetFPS()), 20, 95, 10, LIME);
    if (m_meta.textureRes * m_camera.zoom < 64.0f) DrawText("ZOOM IN TO RENDER TILES", 20, 110, 10, YELLOW);
    if (m_showLegend) drawLegend();
    EndDrawing();
}

void MapViewer::run() {
    InitWindow(1280, 720, "DSA Project - Map Viewer");
    SetTargetFPS(60); m_camera.offset = { (float)GetScreenWidth() / 2.0f, (float)GetScreenHeight() / 2.0f };
    while (!WindowShouldClose() && !m_stopFlag) { handleInput(); updateVisibleTiles(); draw(); }
    std::cout << "Closing Viewer: Unloading textures...\n";
    for (auto& [pos, tex] : m_tileCache) UnloadTexture(tex);
    m_tileCache.clear();
    m_stopFlag = true; CloseWindow();
}

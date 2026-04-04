#include "map_viewer.hpp"
#include <raymath.h>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <cmath>

namespace fs = std::filesystem;

// --- Helper: Scanline Fill for Polygons on CPU Image ---
void fillPolygon(Image* dst, const std::vector<Vector2>& points, Color color) {
    if (points.size() < 3) return;

    int minX = dst->width, maxX = 0, minY = dst->height, maxY = 0;
    for (const auto& p : points) {
        minX = std::min(minX, (int)p.x);
        maxX = std::max(maxX, (int)p.x);
        minY = std::min(minY, (int)p.y);
        maxY = std::max(maxY, (int)p.y);
    }

    minY = std::max(0, minY);
    maxY = std::min(dst->height - 1, maxY);

    for (int y = minY; y <= maxY; y++) {
        std::vector<int> nodes;
        size_t j = points.size() - 1;
        for (size_t i = 0; i < points.size(); i++) {
            if ((points[i].y < (float)y && points[j].y >= (float)y) || (points[j].y < (float)y && points[i].y >= (float)y)) {
                nodes.push_back((int)(points[i].x + (y - points[i].y) / (points[j].y - points[i].y) * (points[j].x - points[i].x)));
            }
            j = i;
        }
        std::sort(nodes.begin(), nodes.end());
        for (size_t i = 0; i < nodes.size(); i += 2) {
            if (i + 1 >= nodes.size()) break;
            int startX = std::max(0, nodes[i]);
            int endX = std::min(dst->width - 1, nodes[i+1]);
            for (int x = startX; x <= endX; x++) {
                ImageDrawPixel(dst, x, y, color);
            }
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
    if (numThreads > 3) numThreads -= 3;
    else numThreads = 1;
    
    std::cout << "Initializing JIT Engine with " << numThreads << " background threads..." << std::endl;
    m_threadsRunning = true;
    for (unsigned int i = 0; i < numThreads; ++i) {
        m_workers.emplace_back(&MapViewer::workerThread, this);
    }
}

MapViewer::~MapViewer() {
    m_threadsRunning = false;
    m_cv.notify_all();
    for (auto& t : m_workers) if (t.joinable()) t.join();
    for (auto& [pos, tex] : m_tileCache) UnloadTexture(tex);
    m_tileCache.clear();
}

void MapViewer::initMetadata(int res, double km) {
    m_meta.textureRes = res;
    m_meta.minLon = m_mapData.bounds.min_lon;
    m_meta.minLat = m_mapData.bounds.min_lat;
    m_meta.maxLon = m_mapData.bounds.max_lon;
    m_meta.maxLat = m_mapData.bounds.max_lat;
    m_meta.degLatPerTile = km / 111.32;
    double centerLat = (m_meta.minLat + m_meta.maxLat) / 2.0;
    m_meta.degLonPerTile = km / (111.32 * std::cos(centerLat * M_PI / 180.0));
    m_meta.cols = std::ceil((m_meta.maxLon - m_meta.minLon) / m_meta.degLonPerTile);
    m_meta.rows = std::ceil((m_meta.maxLat - m_meta.minLat) / m_meta.degLatPerTile);
    m_meta.scaleLon = res / m_meta.degLonPerTile;
    m_meta.scaleLat = res / m_meta.degLatPerTile;
    if (!fs::exists(m_tilesDir)) fs::create_directories(m_tilesDir);
}

void MapViewer::buildSpatialIndex() {
    std::cout << "Indexing " << m_mapData.ways.size() << " ways into tiles..." << std::endl;
    for (const auto& way : m_mapData.ways) {
        if (m_stopFlag) return;
        double wMinLon = 180, wMaxLon = -180, wMinLat = 90, wMaxLat = -90;
        bool hasNodes = false;
        for (long long id : way.node_ids) {
            if (m_mapData.nodes.count(id)) {
                const auto& n = m_mapData.nodes.at(id);
                wMinLon = std::min(wMinLon, n.lon);
                wMaxLon = std::max(wMaxLon, n.lon);
                wMinLat = std::min(wMinLat, n.lat);
                wMaxLat = std::max(wMaxLat, n.lat);
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
    if (m_processingTiles.find({r, c}) != m_processingTiles.end()) return;
    m_processingTiles[{r, c}] = true;
    m_taskQueue.push({r, c});
    m_cv.notify_one();
}

void MapViewer::workerThread() {
    while (m_threadsRunning) {
        std::pair<int, int> tilePos;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait(lock, [this]{ return !m_taskQueue.empty() || !m_threadsRunning; });
            if (!m_threadsRunning) return;
            tilePos = m_taskQueue.front();
            m_taskQueue.pop();
        }

        int r = tilePos.first;
        int c = tilePos.second;
        std::string path = m_tilesDir + "/tile_" + std::to_string(r) + "_" + std::to_string(c) + ".png";
        
        if (!fs::exists(path) && m_buckets.find({r, c}) != m_buckets.end()) {
            double tileMinLon = m_meta.minLon + (c * m_meta.degLonPerTile);
            double tileMaxLat = m_meta.maxLat - (r * m_meta.degLatPerTile);

            Image image = GenImageColor(m_meta.textureRes, m_meta.textureRes, { 5, 5, 10, 255 });

            // Layer 1: Areas (Polygons)
            for (const auto* wayPtr : m_buckets[{r, c}]) {
                const WayData& way = *wayPtr;
                Color fillColor = {0, 0, 0, 0};
                
                if (way.tags.count("natural") && way.tags.at("natural") == "water") fillColor = { 0, 80, 180, 255 }; // Deep Blue
                else if (way.tags.count("landuse")) {
                    std::string l = way.tags.at("landuse");
                    if (l == "grass" || l == "forest" || l == "meadow" || l == "park") fillColor = { 20, 80, 40, 255 }; // Forest Green
                } else if (way.tags.count("building")) fillColor = { 80, 40, 120, 255 }; // Deep Purple

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

            // Layer 2: Lines (Roads, Rails, Power)
            for (const auto* wayPtr : m_buckets[{r, c}]) {
                const WayData& way = *wayPtr;
                Color wayColor = { 80, 80, 100, 255 };
                int thickness = 1;

                if (way.tags.count("highway")) {
                    std::string h = way.tags.at("highway");
                    if (h == "motorway" || h == "trunk") { wayColor = { 255, 80, 0, 255 }; thickness = 4; }
                    else if (h == "primary") { wayColor = { 255, 200, 0, 255 }; thickness = 3; }
                    else if (h == "secondary") { wayColor = { 0, 255, 180, 255 }; thickness = 2; }
                    else if (h == "tertiary") { wayColor = { 200, 200, 255, 255 }; thickness = 1; }
                } else if (way.tags.count("railway") || way.tags.count("power")) { 
                    wayColor = { 255, 50, 150, 255 }; thickness = 2; // Hot Pink
                } else if (way.tags.count("barrier")) {
                    wayColor = { 100, 100, 120, 255 }; thickness = 1;
                } else continue;

                for (size_t i = 0; i < way.node_ids.size() - 1; i++) {
                    if (m_mapData.nodes.count(way.node_ids[i]) && m_mapData.nodes.count(way.node_ids[i+1])) {
                        const auto& n1 = m_mapData.nodes.at(way.node_ids[i]);
                        const auto& n2 = m_mapData.nodes.at(way.node_ids[i+1]);
                        float x1 = (float)((n1.lon - tileMinLon) * m_meta.scaleLon);
                        float y1 = (float)((tileMaxLat - n1.lat) * m_meta.scaleLat);
                        float x2 = (float)((n2.lon - tileMinLon) * m_meta.scaleLon);
                        float y2 = (float)((tileMaxLat - n2.lat) * m_meta.scaleLat);
                        
                        if (thickness > 1) {
                            for (int dx = -thickness/2; dx <= thickness/2; dx++)
                                for (int dy = -thickness/2; dy <= thickness/2; dy++)
                                    ImageDrawLine(&image, (int)x1+dx, (int)y1+dy, (int)x2+dx, (int)y2+dy, wayColor);
                        } else ImageDrawLine(&image, (int)x1, (int)y1, (int)x2, (int)y2, wayColor);
                    }
                }
            }

            if (m_threadsRunning) ExportImage(image, path.c_str());
            UnloadImage(image);
        }
        { std::lock_guard<std::mutex> lock(m_queueMutex); m_processingTiles.erase({r, c}); }
    }
}

void MapViewer::handleInput() {
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 delta = GetMouseDelta();
        delta = Vector2Scale(delta, -1.0f / m_camera.zoom);
        m_camera.target = Vector2Add(m_camera.target, delta);
    }
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), m_camera);
        m_camera.offset = GetMousePosition();
        m_camera.target = mouseWorldPos;
        m_camera.zoom += (wheel * 0.1f * m_camera.zoom);
        if (m_camera.zoom < 0.0001f) m_camera.zoom = 0.0001f; 
        if (m_camera.zoom > 10.0f) m_camera.zoom = 10.0f;
    }
}

void MapViewer::updateVisibleTiles() {
    Vector2 topLeft = GetScreenToWorld2D({0, 0}, m_camera);
    Vector2 bottomRight = GetScreenToWorld2D({(float)GetScreenWidth(), (float)GetScreenHeight()}, m_camera);
    int startCol = floor(topLeft.x / m_meta.textureRes);
    int endCol = ceil(bottomRight.x / m_meta.textureRes);
    int startRow = floor(topLeft.y / m_meta.textureRes);
    int endRow = ceil(bottomRight.y / m_meta.textureRes);
    float tileScreenSize = m_meta.textureRes * m_camera.zoom;
    if (tileScreenSize < 64.0f) {
        if (!m_tileCache.empty()) {
            for (auto& [pos, tex] : m_tileCache) UnloadTexture(tex);
            m_tileCache.clear();
        }
        return;
    }
    std::vector<std::pair<int, int>> currentlyVisible;
    int loadedThisFrame = 0;
    for (int r = startRow; r <= endRow; r++) {
        for (int c = startCol; c <= endCol; c++) {
            if (r >= 0 && r < m_meta.rows && c >= 0 && c < m_meta.cols) {
                if (m_buckets.find({r, c}) == m_buckets.end()) continue;
                currentlyVisible.push_back({r, c});
                if (m_tileCache.find({r, c}) == m_tileCache.end()) {
                    std::string path = m_tilesDir + "/tile_" + std::to_string(r) + "_" + std::to_string(c) + ".png";
                    if (fs::exists(path)) {
                        if (loadedThisFrame < 1 && m_tileCache.size() < 32) {
                            m_tileCache[{r, c}] = LoadTexture(path.c_str());
                            GenTextureMipmaps(&m_tileCache[{r, c}]);
                            SetTextureFilter(m_tileCache[{r, c}], TEXTURE_FILTER_BILINEAR);
                            loadedThisFrame++;
                        }
                    } else enqueueTileGeneration(r, c);
                }
            }
        }
    }
    if (m_tileCache.size() >= 32) {
        for (auto it = m_tileCache.begin(); it != m_tileCache.end();) {
            bool visible = false;
            for (auto& v : currentlyVisible) if (v == it->first) { visible = true; break; }
            if (!visible) { UnloadTexture(it->second); it = m_tileCache.erase(it); }
            else ++it;
        }
    }
}

void MapViewer::draw() {
    BeginDrawing();
    ClearBackground({5, 5, 10, 255});
    BeginMode2D(m_camera);
    for (int r = 0; r < m_meta.rows; r++) {
        for (int c = 0; c < m_meta.cols; c++) {
            DrawRectangleLines(c * m_meta.textureRes, r * m_meta.textureRes, m_meta.textureRes, m_meta.textureRes, Fade(DARKBLUE, 0.3f));
        }
    }
    for (auto& [pos, tex] : m_tileCache) DrawTexture(tex, pos.second * m_meta.textureRes, pos.first * m_meta.textureRes, WHITE);
    
    double homeLon = 80.084, homeLat = 12.919;
    float homeX = (float)((homeLon - m_meta.minLon) * m_meta.scaleLon), homeY = (float)((m_meta.maxLat - homeLat) * m_meta.scaleLat);
    float markerScale = 1.0f / m_camera.zoom, baseRadius = 8.0f * markerScale, pulse = (sin(GetTime() * 4.0f) + 1.0f) * (4.0f * markerScale);
    DrawCircleV({homeX, homeY}, baseRadius * 2.0f + pulse, Fade(LIME, 0.3f));
    DrawCircleV({homeX, homeY}, baseRadius, GREEN);
    DrawCircleV({homeX, homeY}, baseRadius * 0.5f, WHITE);
    float fontSize = 20.0f * markerScale;
    DrawTextEx(GetFontDefault(), "MY HOME", {homeX + baseRadius, homeY - fontSize}, fontSize, 2.0f, GREEN);
    EndMode2D();

    DrawRectangle(10, 10, 300, 120, Fade(BLACK, 0.8f));
    DrawText("VIBRANT JIT MAP ENGINE", 20, 20, 10, SKYBLUE);
    DrawText("Right Click: Pan | Wheel: Zoom", 20, 35, 10, WHITE);
    DrawText(TextFormat("Tiles in VRAM: %d / 32", (int)m_tileCache.size()), 20, 50, 10, GREEN);
    int queued = 0; { std::lock_guard<std::mutex> lock(m_queueMutex); queued = m_taskQueue.size(); }
    DrawText(TextFormat("Tiles Processing: %d", queued), 20, 65, 10, (queued > 0 ? ORANGE : GREEN));
    DrawText(TextFormat("Zoom: %.4f", m_camera.zoom), 20, 80, 10, WHITE);
    DrawText(TextFormat("FPS: %d", GetFPS()), 20, 95, 10, LIME);
    if (m_meta.textureRes * m_camera.zoom < 64.0f) DrawText("ZOOM IN TO RENDER TILES", 20, 110, 10, YELLOW);
    EndDrawing();
}

void MapViewer::run() {
    InitWindow(1280, 720, "DSA Project - Vibrant JIT Map");
    SetTargetFPS(60);
    m_camera.offset = { (float)GetScreenWidth() / 2.0f, (float)GetScreenHeight() / 2.0f };
    while (!WindowShouldClose() && !m_stopFlag) {
        handleInput();
        updateVisibleTiles();
        draw();
    }
    m_stopFlag = true; 
    CloseWindow();
}

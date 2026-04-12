#include "map_viewer.hpp"
#include "path_finding.hpp"
#include <raymath.h>
#include <filesystem>
#include <iostream>
#include <cmath>

namespace fs = std::filesystem;

MapViewer::MapViewer(const std::string& tilesDir, MapData& data, int res, double km) 
    : m_tilesDir(tilesDir), m_mapData(data) {
    initMetadata(res, km);
    buildLabels();
    float worldWidth = (float)(m_meta.cols * m_meta.textureRes);
    float worldHeight = (float)(m_meta.rows * m_meta.textureRes);
    m_camera.offset = { 1280 / 2.0f, 720 / 2.0f };
    m_camera.target = { worldWidth / 2.0f, worldHeight / 2.0f };
    m_camera.rotation = 0.0f;
    m_camera.zoom = 0.01f;
    tilesInVram = 32;
}

MapViewer::~MapViewer() {
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
}

void MapViewer::buildLabels() {
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
        // Landmark Labels
        if (way.tags.count("name") && (way.tags.count("amenity") || way.tags.count("shop") || way.tags.count("university"))) {
            long long midNode = way.node_ids[way.node_ids.size()/2];
            if (m_mapData.nodes.count(midNode)) {
                const auto& n = m_mapData.nodes.at(midNode);
                Vector2 pos = { (float)((n.lon - m_meta.minLon) * m_meta.scaleLon), (float)((m_meta.maxLat - n.lat) * m_meta.scaleLat) };
                m_labels.push_back({ way.tags.at("name"), pos, RAYWHITE, 12 }); // Level 12 for buildings/POIs
            }
        }
    }
}

void MapViewer::handleInput() {
    if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !IsKeyDown(KEY_LEFT_CONTROL)) {
        Vector2 delta = GetMouseDelta(); m_camera.target = Vector2Add(m_camera.target, Vector2Scale(delta, -1.0f / m_camera.zoom));
    }
    
    if (IsKeyDown(KEY_LEFT_CONTROL)) {
        Vector2 mouseWorldPos = GetScreenToWorld2D(GetMousePosition(), m_camera);
        double lon = (mouseWorldPos.x / m_meta.scaleLon) + m_meta.minLon;
        double lat = m_meta.maxLat - (mouseWorldPos.y / m_meta.scaleLat);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            long long id = m_mapData.findNearestNode(lat, lon);
            if (id != -1) {
                m_mapData.startNodeId = id;
                m_mapData.startNode = m_mapData.nodes[id];
                m_mapData.hasStart = true;
                std::cout << "Start point set to Node " << id << ": " << m_mapData.startNode.lat << ", " << m_mapData.startNode.lon << std::endl;
                
                if (m_mapData.hasEnd) {
                    findPath(m_mapData, m_mapData.startNodeId, m_mapData.endNodeId);
                }
            }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            long long id = m_mapData.findNearestNode(lat, lon);
            if (id != -1) {
                m_mapData.endNodeId = id;
                m_mapData.endNode = m_mapData.nodes[id];
                m_mapData.hasEnd = true;
                std::cout << "End point set to Node " << id << ": " << m_mapData.endNode.lat << ", " << m_mapData.endNode.lon << std::endl;
                
                if (m_mapData.hasStart) {
                    findPath(m_mapData, m_mapData.startNodeId, m_mapData.endNodeId);
                }
            }
        }
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

void MapViewer::drawPath() {
    if (m_mapData.pathNodeIds.empty()) return;

    float thickness = 4.0f / m_camera.zoom;
    
    for (size_t i = 0; i < m_mapData.pathNodeIds.size() - 1; i++) {
        long long id1 = m_mapData.pathNodeIds[i];
        long long id2 = m_mapData.pathNodeIds[i+1];
        
        if (m_mapData.nodes.count(id1) && m_mapData.nodes.count(id2)) {
            const auto& n1 = m_mapData.nodes.at(id1);
            const auto& n2 = m_mapData.nodes.at(id2);
            
            Vector2 p1 = { 
                (float)((n1.lon - m_meta.minLon) * m_meta.scaleLon), 
                (float)((m_meta.maxLat - n1.lat) * m_meta.scaleLat) 
            };
            Vector2 p2 = { 
                (float)((n2.lon - m_meta.minLon) * m_meta.scaleLon), 
                (float)((m_meta.maxLat - n2.lat) * m_meta.scaleLat) 
            };
            
            DrawLineEx(p1, p2, thickness, RED);
        }
    }

    if (m_mapData.hasStart) {
        Vector2 startPos = { 
            (float)((m_mapData.startNode.lon - m_meta.minLon) * m_meta.scaleLon), 
            (float)((m_meta.maxLat - m_mapData.startNode.lat) * m_meta.scaleLat) 
        };
        DrawCircleV(startPos, thickness * 1.5f, GREEN);
    }
    if (m_mapData.hasEnd) {
        Vector2 endPos = { 
            (float)((m_mapData.endNode.lon - m_meta.minLon) * m_meta.scaleLon), 
            (float)((m_meta.maxLat - m_mapData.endNode.lat) * m_meta.scaleLat) 
        };
        DrawCircleV(endPos, thickness * 1.5f, BLUE);
    }
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
            if (r >= 0 && r < m_meta.rows && c >= 0 && c < m_meta.cols) {
                visible.push_back({r, c});
                if (!m_tileCache.count({r, c})) {
                    std::string path = m_tilesDir + "/tile_" + std::to_string(r) + "_" + std::to_string(c) + ".png";
                    if (fs::exists(path)) {
                        if (loaded < 1 && m_tileCache.size() < tilesInVram) {
                            m_tileCache[{r, c}] = LoadTexture(path.c_str());
                            GenTextureMipmaps(&m_tileCache[{r, c}]); SetTextureFilter(m_tileCache[{r, c}], TEXTURE_FILTER_BILINEAR);
                            loaded++;
                        }
                    }
                }
            }
        }
    }
    if (m_tileCache.size() >= tilesInVram) {
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
    BeginDrawing();
    ClearBackground({5, 5, 10, 255});
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
    
    drawPath();

    double homeLon = 80.084, homeLat = 12.919;
    float homeX = (float)((homeLon - m_meta.minLon) * m_meta.scaleLon), homeY = (float)((m_meta.maxLat - homeLat) * m_meta.scaleLat);
    float markerScale = 1.0f / m_camera.zoom, baseRadius = 8.0f * markerScale, pulse = (sin(GetTime() * 4.0f) + 1.0f) * (4.0f * markerScale);
    DrawCircleV({homeX, homeY}, baseRadius * 2.0f + pulse, Fade(LIME, 0.3f));
    DrawCircleV({homeX, homeY}, baseRadius, GREEN); DrawCircleV({homeX, homeY}, baseRadius * 0.5f, WHITE);
    DrawTextEx(GetFontDefault(), "HOME", {homeX + baseRadius, homeY - 20.0f * markerScale}, 20.0f * markerScale, 2.0f, GREEN);
    EndMode2D();

    DrawRectangle(10, 10, 300, 140, Fade(BLACK, 0.8f));
    DrawText("MAP VIEWER", 20, 20, 10, SKYBLUE);
    DrawText("Right Click: Pan | Wheel: Zoom", 20, 35, 10, WHITE);
    DrawText(TextFormat("Tiles in VRAM: %d / %d", (int)m_tileCache.size(), (int)tilesInVram), 20, 50, 10, GREEN);
    DrawText(TextFormat("Zoom: %.4f | 'L' for Legend", m_camera.zoom), 20, 65, 10, WHITE);
    DrawText(TextFormat("FPS: %d", GetFPS()), 20, 80, 10, LIME);
    DrawText(TextFormat("Route Distance: %.2f km", m_mapData.pathCost), 20, 95, 10, SKYBLUE);
    if (m_meta.textureRes * m_camera.zoom < 64.0f) DrawText("ZOOM IN TO RENDER TILES", 20, 110, 10, YELLOW);
    if (m_showLegend) drawLegend();
    EndDrawing();
}

void MapViewer::run() {
    InitWindow(1280, 720, "DSA Project - Map Viewer");
    SetTargetFPS(60);
    m_camera.offset = { (float)GetScreenWidth() / 2.0f, (float)GetScreenHeight() / 2.0f };
    while (!WindowShouldClose()){
        handleInput();
        updateVisibleTiles();
        draw();
    }
    std::cout << "Closing Viewer: Unloading textures...\n";
    for (auto& [pos, tex] : m_tileCache) UnloadTexture(tex);
    m_tileCache.clear();
    CloseWindow();
}

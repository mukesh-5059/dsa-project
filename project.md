# Vibrant JIT Map Engine - Project Documentation

## Project Overview
The goal of this project is to build a high-performance, vibrant, and interactive 2D map engine using OpenStreetMap (OSM) data. The engine utilizes a **Just-In-Time (JIT) Rasterization** strategy, where map tiles are generated on-the-fly using multi-threaded CPU rasterization and displayed using the Raylib graphics library.

---

## Core Architecture

### 1. Data Processing & Loading
The engine reads raw geographic data from `.osm.pbf` files.
*   **Tools:** Uses the `osmium` CLI tool for initial map extraction and bounding box refinement.
*   **Library:** Uses `libosmium` for C++ parsing.
*   **Memory Efficiency:** Transitioned from `std::map` to `std::unordered_map` and removed redundant ID storage in `NodeData` to support large datasets (millions of nodes) within reasonable RAM limits.
*   **Files:** `src/map_reader.hpp`, `src/map_reader.cpp`.

### 2. The Unified JIT Engine
The project evolved from a separate pre-processing tool into a unified application that manages both rendering and caching.
*   **Spatial Indexing (Buckets):** On startup, the engine indexes every "Way" into a 2D grid. This allows the engine to instantly know if a tile is empty, avoiding wasted processing.
*   **Thread Pool:** Leverages 13 background threads (on Ryzen 7 7000) to rasterize tiles in parallel while the main thread remains responsive for rendering.
*   **Files:** `src/map_viewer.hpp`, `src/map_viewer.cpp`.

### 3. Rendering & Visuals
The engine features a high-contrast, "vibrant" visual style optimized for a dark background.
*   **Polygon Filling:** Implemented a custom **Scanline Fill Algorithm** to render solid water bodies (Blue), parks/grass (Green), and buildings (Purple).
*   **Line Hierarchy:** 
    *   Motorways/Trunks: 4px Vibrant Orange.
    *   Primary Roads: 3px Vibrant Yellow.
    *   Secondary Roads: 2px Vibrant Cyan.
    *   Railways/Power: 2px Hot Pink.
*   **Hierarchical Labeling:** Administrative names (India -> Tamil Nadu -> Chennai -> Neighbourhoods) are revealed progressively based on zoom level. Labels scale inversely with zoom to remain a constant physical size on screen.
*   **Home Marker:** A pulsing green/white marker at Old Perungalathur coordinates (`12.919, 80.084`).

### 4. Safety & Performance Measures
*   **LOD Cutoff:** Tiles are only loaded if they would appear larger than 64 pixels on screen, preventing massive RAM usage when zoomed out.
*   **VRAM Management:** A strict limit of **32 active textures** in VRAM (~512MB) prevents system crashes.
*   **Graceful Shutdown:** Implemented `SIGINT` (Ctrl+C) handling and explicit texture unloading to prevent segmentation faults and memory leaks on exit.
*   **Throttled Loading:** Loads a maximum of 1 new texture per frame to ensure smooth FPS.

---

## Technical Details

### Administrative Hierarchy
| Zoom Threshold | Admin Level | Example |
| :--- | :--- | :--- |
| **Always** | 2 - 4 | India, Tamil Nadu |
| **> 0.05** | 5 - 6 | Chennai, Kancheepuram |
| **> 0.15** | 8 | Tambaram, Avadi |
| **> 0.40** | 10 | Old Perungalathur |
| **> 0.60** | 12 | VIT Chennai, Landmarks |

### Key Files & Their Purpose
*   `src/main.cpp`: Application entry point, signal handling, and core parameter configuration (`textureRes`, `tileSizeKm`).
*   `src/map_reader.cpp`: Fast PBF parsing and data structure population.
*   `src/map_viewer.cpp`: Main loop, camera control, spatial indexing, multi-threaded JIT rasterization, and legend rendering.
*   `map_data/chennai_city.osm.pbf`: The focused map extract for the Chennai Metropolitan area.
*   `map_data/tag_frequencies.txt`: An automatically generated list of all tags present in the map, used for style categorization.
*   `map_data/place_names.txt`: A generated list of all identified suburbs and neighbourhoods.

---

## Usage Instructions

### Building the Project
```bash
mkdir -p build && cd build
cmake ..
make
```

### Running the Engine
*   **Standard Run:** `./build/DSA_Project`
*   **Fresh Start (Clean Cache):** `./build/DSA_Project --clean`
*   **Custom Map:** `./build/DSA_Project --pbf path/to/map.osm.pbf`

### Controls
*   **Right Click + Drag:** Pan the map.
*   **Mouse Wheel:** Zoom in/out.
*   **'L' Key:** Toggle the Map Legend tab.
*   **Ctrl + C:** Graceful exit.

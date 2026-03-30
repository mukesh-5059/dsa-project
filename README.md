# OSMPathfinder

## Download map

Download the map from this website https://download.geofabrik.de/
download the .osm.pbf file

## Project Structure
```text
.
├── src/                # Source files (.cpp, .hpp)
├── cmake/              # Custom CMake files for osmium and protozero
├── build/              # Build files(make it yourself)
├── CMakeLists.txt      # Main cmake file
└── README.md           
```

## Dependencies

### Linux (Arch btw)
```bash
sudo pacman -Syu base-devel cmake libosmium protozero raylib boost expat zlib bzip2
```

### Windows (vcpkg)
```powershell
vcpkg install libosmium raylib boost-program-options
```

## Build & Run
To compile and execute the project:

### 1. Configure and Build
```bash
# Common for both linux and windows ig
mkdir build
cd build
cmake ..
cmake --build .
```

### 2. Run the Application
```bash
# Linux
./DSA_Project

# Windows
find your exe and update readme
```

## Map Format

### Nodes
```
node 245711916 visible
  version:   4
  changeset: 0
  timestamp: 2025-12-31T21:39:02Z (1767217142)
  user:      0 ""
  lon/lat:   92.7510452,23.1489995
  tags:      1
    "source" = "AND"

```

### Ways
```
way 1491973700 visible
  version:   1
  changeset: 0
  timestamp: 2026-03-23T20:02:17Z (1774296137)
  user:      0 ""
  tags:      1
    "highway" = "residential"
  nodes:     17 (open)
    00: 13672797426
    01: 13672797427
    02: 13672797428
    03: 13672797429
    04: 13672797430
    05: 13672797431
    06: 13672797432
    07: 13672797433
    08: 13672797434
    09: 13672797435
    10: 13672797436
    11: 13672797437
    12: 13672797438
```

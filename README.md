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

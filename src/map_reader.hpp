#ifndef MAP_READER_HPP
#define MAP_READER_HPP

#include <vector>
#include <string>
#include <unordered_map>
#include <osmium/osm/node.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/osm/location.hpp>

struct NodeData {
    double lat;
    double lon;
};

struct WayData {
    long long id;
    std::vector<long long> node_ids;
    std::unordered_map<std::string, std::string> tags;
};

struct MapBounds {
    double min_lat;
    double min_lon;
    double max_lat;
    double max_lon;
};

class MapData {
public:
    std::unordered_map<long long, NodeData> nodes;
    std::vector<WayData> ways;
    MapBounds bounds;

    void loadFromPbf(const std::string& filename);
};

#endif

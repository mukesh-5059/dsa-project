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

struct PlaceData {
    std::string name;
    double lat;
    double lon;
    int admin_level;
};

struct MapBounds {
    double min_lat;
    double min_lon;
    double max_lat;
    double max_lon;
};

struct map_pair_hash {
    inline std::size_t operator()(const std::pair<int, int>& v) const {
        return v.first * 100000 + v.second;
    }
};

struct Edge {
    long long to;
    double weight;
};

class MapData {
public:
    std::unordered_map<long long, NodeData> nodes;
    std::vector<WayData> ways;
    std::vector<PlaceData> places;
    MapBounds bounds;

    std::unordered_map<long long, std::vector<Edge>> adjacencyList;

    std::unordered_map<std::pair<int, int>, std::vector<const WayData*>, map_pair_hash> buckets;
    double degLonPerBucket = 0.01;
    double degLatPerBucket = 0.01;
    int bucketCols = 0;
    int bucketRows = 0;

    NodeData startNode;
    NodeData endNode;
    long long startNodeId = -1;
    long long endNodeId = -1;
    bool hasStart = false;
    bool hasEnd = false;
    std::vector<long long> pathNodeIds;
    double pathCost = 0.0;

    void loadFromPbf(const std::string& filename);
    void buildBuckets(double kmPerBucket = 1.0);
    void makeAdjacencyList();
    long long findNearestNode(double lat, double lon);
};

#endif

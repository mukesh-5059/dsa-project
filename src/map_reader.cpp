#include "map_reader.hpp"
#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>
#include <iostream>
#include <limits>
#include <cmath>

using index_type = osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location>;
using location_handler_type = osmium::handler::NodeLocationsForWays<index_type>;

// Helper to guess admin level if missing
int placeToAdminLevel(const char* place) {
    if (!place) return 10;
    std::string p = place;
    if (p == "country") return 2;
    if (p == "state") return 4;
    if (p == "city") return 6;
    if (p == "town") return 8;
    if (p == "suburb") return 9;
    if (p == "neighbourhood" || p == "locality") return 10;
    return 10;
}

class OsmHandler : public osmium::handler::Handler {
    MapData& m_data;
public:
    OsmHandler(MapData& data) : m_data(data) {}

    void node(const osmium::Node& node) {
        m_data.nodes[node.id()] = {node.location().lat(), node.location().lon()};
        
        // Capture Places
        const char* place = node.tags().get_value_by_key("place");
        const char* name = node.tags().get_value_by_key("name");
        if (place && name) {
            PlaceData pd;
            pd.name = name;
            pd.lat = node.location().lat();
            pd.lon = node.location().lon();
            const char* al = node.tags().get_value_by_key("admin_level");
            pd.admin_level = al ? std::stoi(al) : placeToAdminLevel(place);
            m_data.places.push_back(pd);
        }
    }

    void way(const osmium::Way& way) {
        WayData wd;
        wd.id = way.id();
        for (const auto& node_ref : way.nodes()) wd.node_ids.push_back(node_ref.ref());
        for (const auto& tag : way.tags()) wd.tags[tag.key()] = tag.value();
        m_data.ways.push_back(wd);
    }
};

void MapData::loadFromPbf(const std::string& filename) {
    nodes.clear(); ways.clear(); places.clear();
    osmium::io::Reader reader{filename, osmium::osm_entity_bits::node | osmium::osm_entity_bits::way};
    index_type index;
    location_handler_type location_handler{index};
    OsmHandler handler{*this};
    std::cout << "Parsing PBF file..." << std::endl;
    osmium::apply(reader, location_handler, handler);
    reader.close();

    this->bounds.min_lat = 12.79;
    this->bounds.max_lat = 13.18;
    this->bounds.min_lon = 79.98;
    this->bounds.max_lon = 80.34;

    //if (!nodes.empty()) {
    //    this->bounds = {90.0, 180.0, -90.0, -180.0};
    //    for (const auto& [id, node] : nodes) {
    //        if (node.lat < bounds.min_lat) bounds.min_lat = node.lat;
    //        if (node.lon < bounds.min_lon) bounds.min_lon = node.lon;
    //        if (node.lat > bounds.max_lat) bounds.max_lat = node.lat;
    //        if (node.lon > bounds.max_lon) bounds.max_lon = node.lon;
    //    }
    //}
    std::cout << "Loaded " << nodes.size() << " nodes, " << ways.size() << " ways, and " << places.size() << " places.\n";
}

void MapData::buildBuckets(double kmPerBucket) {
    buckets.clear();
    degLatPerBucket = kmPerBucket / 111.32;
    double centerLat = (bounds.min_lat + bounds.max_lat) / 2.0;
    degLonPerBucket = kmPerBucket / (111.32 * std::cos(centerLat * M_PI / 180.0));
    bucketCols = std::ceil((bounds.max_lon - bounds.min_lon) / degLonPerBucket);
    bucketRows = std::ceil((bounds.max_lat - bounds.min_lat) / degLatPerBucket);

    std::cout << "Building " << bucketCols << "x" << bucketRows << " buckets for rendering and fast lookup..." << std::endl;

    for (const auto& way : ways) {
        double wMinLon = 180, wMaxLon = -180, wMinLat = 90, wMaxLat = -90;
        bool hasNodes = false;
        for (long long id : way.node_ids) {
            if (nodes.count(id)) {
                const auto& n = nodes.at(id);
                wMinLon = std::min(wMinLon, n.lon); wMaxLon = std::max(wMaxLon, n.lon);
                wMinLat = std::min(wMinLat, n.lat); wMaxLat = std::max(wMaxLat, n.lat);
                hasNodes = true;
            }
        }
        if (!hasNodes) continue;

        int cStart = std::max(0, (int)floor((wMinLon - bounds.min_lon) / degLonPerBucket));
        int cEnd   = std::min(bucketCols - 1, (int)floor((wMaxLon - bounds.min_lon) / degLonPerBucket));
        int rStart = std::max(0, (int)floor((bounds.max_lat - wMaxLat) / degLatPerBucket));
        int rEnd   = std::min(bucketRows - 1, (int)floor((bounds.max_lat - wMinLat) / degLatPerBucket));

        for (int r = rStart; r <= rEnd; r++) {
            for (int c = cStart; c <= cEnd; c++) buckets[{r, c}].push_back(&way);
        }
    }
}

long long MapData::findNearestNode(double lat, double lon) {
    int c = (int)floor((lon - bounds.min_lon) / degLonPerBucket);
    int r = (int)floor((bounds.max_lat - lat) / degLatPerBucket);

    long long nearestId = -1;
    double minDistance = std::numeric_limits<double>::max();

    auto searchInBucket = [&](int br, int bc) {
        if (br < 0 || br >= bucketRows || bc < 0 || bc >= bucketCols) return;
        if (buckets.count({br, bc})) {
            for (const auto* wayPtr : buckets.at({br, bc})) {
                // User requirement: only roads/highways
                if (!wayPtr->tags.count("highway")) continue;

                for (long long id : wayPtr->node_ids) {
                    if (nodes.count(id)) {
                        const auto& node = nodes.at(id);
                        double dLat = node.lat - lat;
                        double dLon = node.lon - lon;
                        double distSq = dLat * dLat + dLon * dLon;
                        if (distSq < minDistance) {
                            minDistance = distSq;
                            nearestId = id;
                        }
                    }
                }
            }
        }
    };

    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            searchInBucket(r + dr, c + dc);
        }
    }

    if (nearestId == -1) {
        std::cout << "Warning: No road node found in local buckets, performing full search..." << std::endl;
        for (const auto& [id, node] : nodes) {
            double dLat = node.lat - lat;
            double dLon = node.lon - lon;
            double distSq = dLat * dLat + dLon * dLon;
            if (distSq < minDistance) {
                minDistance = distSq;
                nearestId = id;
            }
        }
    }
    return nearestId;
}

double distanceBetweenPoints(const NodeData& a, const NodeData& b) {
    const double R = 6371.0; // km
    double dLat = (b.lat - a.lat) * M_PI / 180.0;
    double dLon = (b.lon - a.lon) * M_PI / 180.0;
    double lat1 = a.lat * M_PI / 180.0;
    double lat2 = b.lat * M_PI / 180.0;
    double x = sin(dLat / 2.0) * sin(dLat / 2.0) +
               sin(dLon / 2.0) * sin(dLon / 2.0) * cos(lat1) * cos(lat2);
    double y = 2.0 * atan2(sqrt(x), sqrt(1.0 - x));
    return R * y;
}

void MapData::makeAdjacencyList() {
    adjacencyList.clear();
    std::cout << "Building adjacency list from road network..." << std::endl;
    int edgesCount = 0;
    for (const auto& way : ways) {
        if (!way.tags.count("highway")) continue;
        bool oneway = false;
        if (way.tags.count("oneway")) {
            std::string val = way.tags.at("oneway");
            if (val == "yes" || val == "1" || val == "true") oneway = true;
        }
        for (size_t i = 0; i < way.node_ids.size() - 1; i++) {
            long long u = way.node_ids[i];
            long long v = way.node_ids[i+1];
            if (nodes.count(u) && nodes.count(v)) {
                double dist = distanceBetweenPoints(nodes[u], nodes[v]);
                adjacencyList[u].push_back({v, dist});
                edgesCount++;
                if (!oneway) {
                    adjacencyList[v].push_back({u, dist});
                    edgesCount++;
                }
            }
        }
    }
    std::cout << "Adjacency list built with " << adjacencyList.size() << " vertices and " << edgesCount << " edges." << std::endl;
}

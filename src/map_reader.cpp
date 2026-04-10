#include "map_reader.hpp"
#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>
#include <iostream>

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

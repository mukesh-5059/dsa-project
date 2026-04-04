#include "map_reader.hpp"
#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/index/map/sparse_mem_array.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>
#include <iostream>

using index_type = osmium::index::map::SparseMemArray<osmium::unsigned_object_id_type, osmium::Location>;
using location_handler_type = osmium::handler::NodeLocationsForWays<index_type>;

class OsmHandler : public osmium::handler::Handler {
    MapData& m_data;
public:
    OsmHandler(MapData& data) : m_data(data) {}

    void node(const osmium::Node& node) {
        m_data.nodes[node.id()] = {node.location().lat(), node.location().lon()};
    }

    void way(const osmium::Way& way) {
        WayData wd;
        wd.id = way.id();
        for (const auto& node_ref : way.nodes()) {
            wd.node_ids.push_back(node_ref.ref());
        }
        for (const auto& tag : way.tags()) {
            wd.tags[tag.key()] = tag.value();
        }
        m_data.ways.push_back(wd);
    }
};

void MapData::loadFromPbf(const std::string& filename) {
    osmium::io::Reader reader{filename, osmium::osm_entity_bits::node | osmium::osm_entity_bits::way};
    
    // Get bounds from header if available
    osmium::io::Header header = reader.header();
    auto bounds_list = header.boxes();
    bool has_header_bounds = false;
    if (!bounds_list.empty()) {
        this->bounds = {
            bounds_list[0].bottom_left().lat(),
            bounds_list[0].bottom_left().lon(),
            bounds_list[0].top_right().lat(),
            bounds_list[0].top_right().lon()
        };
        has_header_bounds = true;
    }

    index_type index;
    location_handler_type location_handler{index};
    OsmHandler handler{*this};

    osmium::apply(reader, location_handler, handler);
    reader.close();

    if (!has_header_bounds) {
        // Fallback: Calculate bounds from nodes AFTER they are loaded
        this->bounds = {90.0, 180.0, -90.0, -180.0};
        for (const auto& [id, node] : nodes) {
            if (node.lat < bounds.min_lat) bounds.min_lat = node.lat;
            if (node.lon < bounds.min_lon) bounds.min_lon = node.lon;
            if (node.lat > bounds.max_lat) bounds.max_lat = node.lat;
            if (node.lon > bounds.max_lon) bounds.max_lon = node.lon;
        }
    }

    std::cout << "Loaded " << nodes.size() << " nodes and " << ways.size() << " ways.\n";
    std::cout << "Bounds: " << bounds.min_lon << "," << bounds.min_lat << " to " << bounds.max_lon << "," << bounds.max_lat << "\n";
}

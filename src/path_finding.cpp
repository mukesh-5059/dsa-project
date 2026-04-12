#include "path_finding.hpp"
#include <iostream>
#include <queue>
#include <algorithm>
#include <cmath>

void findPath(MapData& data, long long startId, long long endId) {
    data.pathNodeIds.clear();
    if (startId == -1 || endId == -1) return;
    if (startId == endId) { data.pathNodeIds.push_back(startId); return; }

    std::unordered_map<long long, double> distances;
    std::unordered_map<long long, long long> parent;
    using PQItem = std::pair<double, long long>;
    std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> pq;

    distances[startId] = 0.0;
    pq.push({0.0, startId});

    bool found = false;
    while (!pq.empty()) {
        double d = pq.top().first;
        long long u = pq.top().second;
        pq.pop();
        if (u == endId) { found = true; break; }
        if (d > distances[u]) continue;
        if (data.adjacencyList.count(u)) {
            for (const auto& edge : data.adjacencyList.at(u)) {
                double newDist = d + edge.weight;
                if (!distances.count(edge.to) || newDist < distances[edge.to]) {
                    distances[edge.to] = newDist;
                    parent[edge.to] = u;
                    pq.push({newDist, edge.to});
                }
            }
        }
    }
    if (found) {
        long long curr = endId;
        while (curr != startId) {
            data.pathNodeIds.push_back(curr);
            curr = parent[curr];
        }
        data.pathNodeIds.push_back(startId);
        std::reverse(data.pathNodeIds.begin(), data.pathNodeIds.end());
        data.pathCost = distances[endId];
    } else {
        data.pathCost = 0.0;
    }
}

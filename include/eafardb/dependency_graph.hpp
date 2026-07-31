#pragma once
// eafardb/dependency_graph.hpp — S6: declared view dependencies.
//
// A view chain is declared as edges in a directed graph (source -> dependent).
// The graph validates two invariants:
//   * no cycles  (a view must not depend on itself transitively)
//   * no undeclared edges (both endpoints must be declared nodes)
// Violations throw std::runtime_error at declaration time.

#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace eafardb {

class DependencyGraph {
public:
    void declare_node(std::string name) {
        if (nodes_.contains(name)) {
            throw std::runtime_error("eafardb: duplicate node '" + name + "'");
        }
        nodes_.insert(std::move(name));
    }

    // Adds edge source -> dependent. Throws if either endpoint is
    // undeclared, or if the edge would create a cycle.
    void add_edge(const std::string& source, const std::string& dependent) {
        if (!nodes_.contains(source)) {
            throw std::runtime_error("eafardb: undeclared node '" + source + "'");
        }
        if (!nodes_.contains(dependent)) {
            throw std::runtime_error("eafardb: undeclared node '" + dependent + "'");
        }
        // Cycle check: is `source` reachable from `dependent`?
        // If yes, adding dependent -> source closes a loop.
        if (reachable(dependent, source)) {
            throw std::runtime_error("eafardb: dependency cycle: '" +
                                     dependent + "' -> '" + source + "'");
        }
        edges_[source].insert(dependent);
    }

    bool has_node(const std::string& name) const { return nodes_.contains(name); }

private:
    // DFS: can we reach `target` starting from `start` following edges?
    bool reachable(const std::string& start, const std::string& target) const {
        std::set<std::string> visited;
        return dfs(start, target, visited);
    }

    bool dfs(const std::string& node, const std::string& target,
             std::set<std::string>& visited) const {
        if (node == target) {
            return true;
        }
        if (visited.contains(node)) {
            return false;
        }
        visited.insert(node);
        auto it = edges_.find(node);
        if (it == edges_.end()) {
            return false;
        }
        for (const auto& next : it->second) {
            if (dfs(next, target, visited)) {
                return true;
            }
        }
        return false;
    }

    std::set<std::string> nodes_;
    std::map<std::string, std::set<std::string>> edges_;
};

} // namespace eafardb

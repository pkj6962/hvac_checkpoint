#ifndef HASH_RING_H
#define HASH_RING_H

#include <iostream>
#include <map>
#include <string>
#include <functional>
#include <set>
#include <type_traits>
#include <stdexcept>

using namespace std;

template <class Node, class Data, class Hash = hash<string> >
class HashRing {
public:
    typedef map<size_t, Node> NodeMap;
    HashRing(unsigned int replicas)
        : replicas_(replicas), hash_(Hash()) {
    }
    HashRing(unsigned int replicas, const Hash& hash)
        : replicas_(replicas), hash_(hash) {
    }
    size_t AddNode(const Node& node);
    void RemoveNode(const Node& node);
    const Node& GetNode(const Data& data) const;
    int AdjacentNodes(const Node& node) const;
    int ConvertHostToNumber(const Node& node) const;
private:
    string StringifyNode(const Node& node) const {
        if constexpr (is_same_v<Node, string>) {
            return node;
        } else {
            return to_string(node);
        }
    }
    string StringifyReplica(unsigned int num) const {
        return to_string(num);
    }
    string StringifyData(const Data& data) const {
        if constexpr (is_same_v<Data, string>) {
            return data;
        } else {
            return to_string(data);
        }
    }
    NodeMap ring_;
    const unsigned int replicas_;
    Hash hash_;
};

template <class Node, class Data, class Hash>
size_t HashRing<Node, Data, Hash>::AddNode(const Node& node) {
    size_t hash;
    string nodestr = StringifyNode(node);
    for (unsigned int r = 0; r < replicas_; r++) {
        hash = hash_((nodestr + StringifyReplica(r)).c_str());
        ring_[hash] = node;
    }
    return hash;
}

template <class Node, class Data, class Hash>
void HashRing<Node, Data, Hash>::RemoveNode(const Node& node) {
    string nodestr = StringifyNode(node);
    for (unsigned int r = 0; r < replicas_; r++) {
        size_t hash = hash_((nodestr + StringifyReplica(r)).c_str());
        ring_.erase(hash);
    }
}

template <class Node, class Data, class Hash>
const Node& HashRing<Node, Data, Hash>::GetNode(const Data& data) const {
    if (ring_.empty()) {
        throw runtime_error("Empty ring");
    }
    size_t hash = hash_(StringifyData(data).c_str());
    typename NodeMap::const_iterator it;
    it = ring_.lower_bound(hash);
    if (it == ring_.end()) {
        it = ring_.begin();
    }
    return it->second;
}

template <class Node, class Data, class Hash>
int HashRing<Node, Data, Hash>::AdjacentNodes(const Node& node) const {
    string nodestr = StringifyNode(node);
    set<int> nodes;
    for (unsigned int r = 0; r < replicas_; r++) {
        size_t hash = hash_((nodestr + StringifyReplica(r)).c_str());
        typename NodeMap::const_iterator it = ring_.find(hash);
        if (it != ring_.end()) {
            typename NodeMap::const_iterator prev_it = it == ring_.begin() ? --ring_.end() : prev(it);
            typename NodeMap::const_iterator next_it = next(it) == ring_.end() ? ring_.begin() : next(it);
            nodes.insert(prev_it->second);
            nodes.insert(next_it->second);
        }
    }
    return nodes.size();
}

template <class Node, class Data, class Hash>
int HashRing<Node, Data, Hash>::ConvertHostToNumber(const Node& node) const {
    string nodestr = StringifyNode(node);
    if (nodestr.find("server") != string::npos) {
        return stoi(nodestr.substr(6)) - 1;
    }
    throw runtime_error("Invalid node name format");
}

#endif // HASH_RING_H


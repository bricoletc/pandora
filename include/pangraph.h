#ifndef __PANGRAPH_H_INCLUDED__   // if pangraph.h hasn't been included yet...
#define __PANGRAPH_H_INCLUDED__

class PanNode;
class PanEdge;
class PanRead;
struct MinimizerHit;

#include <cstring>
#include <map>
#include <ostream>
#include <functional>
#include <minihits.h>

class PanGraph {
  public:
    std::map<uint32_t, PanNode*> nodes;
    std::vector<PanEdge*> edges;
    std::map<uint32_t, PanRead*> reads;

    PanGraph() {};
    ~PanGraph();

    void add_node (const uint32_t, const std::string, uint32_t, const std::set<MinimizerHit*, pComp>&);
    PanEdge* add_edge (const uint32_t&, const uint32_t&, const uint&);
    void add_edge (const uint32_t&, const uint32_t&, const uint&, const uint&);
    void read_clean(const uint&);
    void remove_low_covg_nodes(const uint&);
    void remove_low_covg_edges(const uint&);
    void clean(const uint32_t&);
    bool operator == (const PanGraph& y) const;
    void write_gfa (const std::string&);
    friend std::ostream& operator<< (std::ostream& out, const PanGraph& m);
};

#endif

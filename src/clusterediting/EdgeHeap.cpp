#include "EdgeHeap.h"
#include "ProgressPrinter.h"
#include <cmath>
#include <algorithm>
  
using Edge = DynamicSparseGraph::Edge;
using EdgeWeight = DynamicSparseGraph::EdgeWeight;
using EdgeId = DynamicSparseGraph::EdgeId;
using RankId = DynamicSparseGraph::RankId;
using NodeId = DynamicSparseGraph::NodeId;

EdgeHeap::EdgeHeap(StaticSparseGraph& param_graph) :
    graph(param_graph),
    unprocessed(0),
    edges(1+param_graph.numEdges(), DynamicSparseGraph::InvalidEdge),
    icf(1+param_graph.numEdges(), DynamicSparseGraph::Forbidden),
    icp(1+param_graph.numEdges(), DynamicSparseGraph::Forbidden),
    edge2forb_rank(1+param_graph.numEdges(), 0),
    edge2perm_rank(1+param_graph.numEdges(), 0),
    edgeToBundle(1+param_graph.numEdges(), 0),
    edgeBundles(1+param_graph.numEdges(), std::vector<RankId>(0))
{}

void EdgeHeap::initInducedCosts() {
    uint64_t numNodes = graph.numNodes();
    ProgressPrinter pp("Precompute induced costs", 0, 1+(numNodes*(numNodes-1UL))/2UL);
    // compute array: edge -> icf/icp
    for (NodeId u = 0; u < numNodes; u++) {
        for (NodeId v : graph.getNonZeroNeighbours(u)) {
            if (v < u)
                continue;
            
            // iterate over all edges uv
            Edge uv(u,v);
            EdgeId id = uv.id();
            RankId rId = graph.findIndex(id);
            
            // Zero edges have no icp/icf
            if (rId == 0) {
                continue;
            } else {
                edges[rId] = uv;
            }
            
            EdgeWeight w_uv = graph.getWeight(rId);

            if (w_uv == 0.0 || w_uv == DynamicSparseGraph::Forbidden || w_uv == DynamicSparseGraph::Permanent) {
                continue;
            } else {
                icf[rId] = 0.0;
                icp[rId] = 0.0;
                unprocessed++;
            }
            
            // costs for the edge uv itself
            if (w_uv >= 0) {	
                icf[rId] += w_uv;	// costs for removing uv
            } else {
                icp[rId] += -w_uv;	// costs for adding uv
            }
            
            // look at all triangles uvw containing uv. Triangles with a zero edge can be ignored
            std::vector<NodeId> w_vec;
            std::set_intersection(graph.getNonZeroNeighbours(u).begin(), graph.getNonZeroNeighbours(u).end(), 
                                  graph.getNonZeroNeighbours(v).begin(), graph.getNonZeroNeighbours(v).end(), back_inserter(w_vec));

            for (NodeId w : w_vec) {
                Edge uw(u,w);
                Edge vw(v,w);
                EdgeWeight w_uw = graph.getWeight(uw);
                EdgeWeight w_vw = graph.getWeight(vw);
                icf[rId] += getIcf(w_uw, w_vw);
                icp[rId] += getIcp(w_uw, w_vw);
            }
        }
        pp.setProgress(((2UL*numNodes-(uint64_t)u+1UL)*(uint64_t)u)/2UL);
    }
    
    for (unsigned int i = 0; i < icf.size(); i++){
        if(std::isnan(icf[graph.findIndex(i)])) {
            std::cout<<"NaN! in icf"<<std::endl;
            break;
        }
        if(std::isnan(icp[graph.findIndex(i)])) {
            std::cout<<"NaN! in icp"<<std::endl;
            break;
        }
    }
    
    // sort edges by icf and icp values
    for (RankId id = 0; id < icf.size(); id++) {
        forb_rank2edge.push_back(id);
        perm_rank2edge.push_back(id);
    }
    
    std::sort(forb_rank2edge.begin(), forb_rank2edge.end(), [this] (const EdgeId& a, const EdgeId& b) { return icf[a] > icf[b]; });
    std::sort(perm_rank2edge.begin(), perm_rank2edge.end(), [this] (const EdgeId& a, const EdgeId& b) { return icp[a] > icp[b]; });
    
    // save index in sorted vectors for each edge
    for (RankId i = 0; i < icf.size(); i++) {
        edge2forb_rank[forb_rank2edge[i]] = i;
        edge2perm_rank[perm_rank2edge[i]] = i;
    }
    
    // initialize edge bundles
    for (RankId id = 0; id < icf.size(); id++) {
        edgeToBundle[id] = id;
        edgeBundles[id].push_back(id);
    }

    pp.setFinished();
}

Edge EdgeHeap::getMaxIcfEdge() const {
    RankId ei = forb_rank2edge[0];
    if (forb_rank2edge.size() <= 1) {
        // only rank 0 entry left
        return DynamicSparseGraph::InvalidEdge;
    }
    if (icf[ei] < 0) {
        return DynamicSparseGraph::InvalidEdge;
    }
    if (verbosity >= 6) {
        std::cout<<"icf heap: ";
        for (unsigned int i = 0; i < icf.size(); i++) {
            RankId rid = forb_rank2edge[i];
            Edge e = edges[rid];
            std::cout << "("<<e.u<<","<<e.v<<")="<<icf[rid]<<" ; ";
        }
        std::cout<<std::endl;
    } else if (verbosity >= 4) {
        std::cout<<"Max icf edge = ("<<ei<<") = ("<<edges[ei].u<<","<<edges[ei].v<<") weight ("<<icf[ei]<<")"<<std::endl;
    }
    return edges[ei];
}

Edge EdgeHeap::getMaxIcpEdge() const {
    RankId ei = perm_rank2edge[0];
    if (perm_rank2edge.size() <= 1) {
        // only rank 0 entry left
        return DynamicSparseGraph::InvalidEdge;
    }
    if (icp[ei] < 0) {
        return DynamicSparseGraph::InvalidEdge;
    }
    if (verbosity >= 6) {
        std::cout<<"icp heap: ";
        for (unsigned int i = 0; i < icp.size(); i++) {
            RankId rid = perm_rank2edge[i];
            Edge e = edges[rid];
            std::cout << "("<<e.u<<","<<e.v<<")="<<icp[rid]<<" ; ";
        }
        std::cout<<std::endl;
    } else if (verbosity >= 4) {
        std::cout<<"Max icp edge = ("<<ei<<") = ("<<edges[ei].u<<","<<edges[ei].v<<") weight ("<<icp[ei]<<")"<<std::endl;
    }
    return edges[ei];
}

EdgeWeight EdgeHeap::getIcf(const Edge e) const {
    if (graph.findIndex(e) == 0)
        std::cout<<"getIcf on edge with rank 0"<<std::endl;
    return icf[edgeToBundle[graph.findIndex(e)]];
}

EdgeWeight EdgeHeap::getIcp(const Edge e) const {
    if (graph.findIndex(e) == 0)
        std::cout<<"getIcf on edge with rank 0"<<std::endl;
    return icp[edgeToBundle[graph.findIndex(e)]];
}

void EdgeHeap::increaseIcf(const Edge e, const EdgeWeight w) {
    RankId rId = graph.findIndex(e);
    if (rId > 0 && w != 0 && icf[edgeToBundle[rId]] >= 0) {
        RankId eb = edgeToBundle[rId];
        icf[eb] += w;
        icf[eb] = std::max(icf[eb], 0.0);
        updateHeap(forb_rank2edge, eb, w, edge2forb_rank, icf);
    }
}

void EdgeHeap::increaseIcp(const Edge e, const EdgeWeight w) {
    RankId rId = graph.findIndex(e);
    if (rId > 0 && w != 0 && icp[edgeToBundle[rId]] >= 0) {
        RankId eb = edgeToBundle[rId];
        icp[eb] += w;
        icp[eb] = std::max(icp[eb], 0.0);
        updateHeap(perm_rank2edge, eb, w, edge2perm_rank, icp);
    }
}

void EdgeHeap::mergeEdges(const Edge e1, const Edge e2) {
    RankId r1 = graph.findIndex(e1);
    RankId r2 = graph.findIndex(e2);
    if ((r1 & r2) == 0)
        return;
    RankId eb1 = edgeToBundle[r1];
    RankId eb2 = edgeToBundle[r2];
    if (eb1 == eb2)
        return;
    
    if (edgeBundles[eb1].size() > edgeBundles[eb2].size()) {
        for (RankId toDelete : edgeBundles[eb2]) {
            edgeBundles[eb1].push_back(toDelete);
            edgeToBundle[toDelete] = eb1;
        }
        edgeBundles[eb2].clear();
        if (icf[eb2] < 0.0) {
            std::cout<<"Merged edge has negative icf"<<std::endl;
        } else {
            icf[eb1] += icf[eb2];
        }
        if (icp[eb2] < 0.0) {
            std::cout<<"Merged edge has negative icp"<<std::endl;
        } else {
            icp[eb1] += icp[eb2];
        }
        removeEdge(eb2);
    } else {
        for (RankId toDelete : edgeBundles[eb1]) {
            edgeBundles[eb2].push_back(toDelete);
            edgeToBundle[toDelete] = eb2;
        }
        edgeBundles[eb1].clear();
        if (icf[eb1] < 0.0) {
            std::cout<<"Merged edge has negative icf"<<std::endl;
        } else {
            icf[eb2] += icf[eb1];
        }
        if (icp[eb1] < 0.0) {
            std::cout<<"Merged edge has negative icp"<<std::endl;
        } else {
            icp[eb2] += icp[eb1];
        }
        removeEdge(eb1);
    }
}

void EdgeHeap::removeEdge(const Edge e) {
    removeEdge(graph.findIndex(e));
}

void EdgeHeap::removeEdge(const RankId rId) {
    if (rId == 0) {
        return;
    }
    else if (verbosity >= 4) {
        std::cout<<"Removing edge ("<<edges[rId].u<<","<<edges[rId].v<<") from heap ("<<rId<<")"<<std::endl;
    }
    if (rId > 0 && icf[rId] != DynamicSparseGraph::Forbidden && icp[rId] != DynamicSparseGraph::Forbidden) {
        icf[rId] = DynamicSparseGraph::Forbidden;
        icp[rId] = DynamicSparseGraph::Forbidden;
        updateHeap(forb_rank2edge, rId, DynamicSparseGraph::Forbidden, edge2forb_rank, icf);
        updateHeap(perm_rank2edge, rId, DynamicSparseGraph::Forbidden, edge2perm_rank, icp);
        unprocessed--;
    }
}

uint64_t EdgeHeap::numUnprocessed() const {
    return unprocessed;
}

void EdgeHeap::updateHeap(std::vector<RankId>& heap, const RankId e, const EdgeWeight change, std::vector<RankId>& index, const std::vector<EdgeWeight>& score) {
    uint64_t pos = index[e];
    /*
     * index arithemetic for zero based array: parent = (index-1)/2, children = 2*index+1 and 2*index+2
     */
    if (change > 0) {
        // value increased -> move edge upwards in heap
        uint64_t parent = (pos-1)/2;
        while(pos > 0 && score[heap[parent]] < score[heap[pos]]) {
            // swap pos and pos/2
            std::swap(heap[pos], heap[parent]);
            index[heap[pos]] = pos;
            index[heap[parent]] = parent;
            pos = parent;
            parent = (pos-1)/2;
        }
    } else {
        // value decreased -> move edge downwards in heap
        uint64_t lChild = 2*pos+1;
        uint64_t rChild = 2*pos+2;
        while((lChild < heap.size() && score[heap[pos]] < score[heap[lChild]])
            | (rChild < heap.size() && score[heap[pos]] < score[heap[rChild]]) ) {
            if (rChild < heap.size() && score[heap[lChild]] < score[heap[rChild]]) {
                // right child exists and is larger than left child -> swap pos with right child
                std::swap(heap[pos], heap[rChild]);
                index[heap[pos]] = pos;
                index[heap[rChild]] = rChild;
                pos = rChild;
            } else {
                // else swap with left child
                std::swap(heap[pos], heap[lChild]);
                index[heap[pos]] = pos;
                index[heap[lChild]] = lChild;
                pos = lChild;
            }
            lChild = 2*pos+1;
            rChild = 2*pos+2;
        }
    }
}

#ifndef HAVOQGT_LABEL_PROPAGATION_PATTERN_MATCHING_HPP_INCLUDED
#define HAVOQGT_LABEL_PROPAGATION_PATTERN_MATCHING_HPP_INCLUDED

#include <havoqgt/visitor_queue.hpp>
#include <havoqgt/detail/visitor_priority_queue.hpp>

namespace havoqgt { namespace mpi {

template<typename IntegralType>
class vertex_state {
public:
  vertex_state() :
  global_itr_count(0),
  local_itr_count(0) {
  }

  IntegralType global_itr_count;
  IntegralType local_itr_count;
  std::unordered_map<size_t, IntegralType> pattern_vertex_itr_count_map; 
};

template<typename Visitor>
class lppm_queue {

public:
  lppm_queue() {}

  bool push(Visitor const& element) {
    data.push_back(element);
    return true;
  }

  void pop() {
    data.pop_back();
  }
 
  Visitor const& top() {
    return data.back();
  } 
  
  size_t size() const {
    return data.size();;
  }

  bool empty() const {
    return data.empty();
  }

  void clear() {
    data.clear();
  }

protected:
  std::vector<Visitor> data;

};

// label propagation pattern matching visitor class
template<typename Graph, typename VertexData>
class lppm_visitor {
public:
  typedef typename Graph::vertex_locator vertex_locator;
  typedef typename Graph::edge_iterator eitr_type;

  lppm_visitor() : 
    itr_count(0),
    do_update_vertex_pattern_id(false) {}

  lppm_visitor(vertex_locator _vertex, uint64_t _itr_count = 0, 
    bool _do_update_vertex_pattern_id = false) : 
    vertex(_vertex), 
    itr_count(_itr_count), 
    do_update_vertex_pattern_id(_do_update_vertex_pattern_id) {}

  lppm_visitor(vertex_locator _vertex, vertex_locator _parent, 
    VertexData _parent_vertex_data, size_t _parent_pattern_index, 
    uint64_t _itr_count, bool _do_update_vertex_pattern_id = false) :
    vertex(_vertex), 
    parent(_parent),
    parent_vertex_data(_parent_vertex_data),
    parent_pattern_index(_parent_pattern_index), 
    itr_count(_itr_count), 
    do_update_vertex_pattern_id(_do_update_vertex_pattern_id) {}

  ~lppm_visitor() {}

  template<typename AlgData> 
  bool pre_visit(AlgData& alg_data) const {
    if (!std::get<4>(alg_data)[vertex]) {
      return false;
    }

    auto vertex_data = std::get<0>(alg_data)[vertex];
    auto pattern = std::get<1>(alg_data);
    auto pattern_indices = std::get<2>(alg_data);

    // TODO: update veretx_pattern_id
    // need to write a new constructor to do update only
    //std::get<5>(alg_data)[vertex] = vertex_pattern_index; 

    return true;
  }
  
  template<typename VisitorQueueHandle, typename AlgData>
  bool init_visit(Graph& g, VisitorQueueHandle vis_queue, 
    AlgData& alg_data) const {
    return visit(g, vis_queue, alg_data);
  }

  template<typename VisitorQueueHandle, typename AlgData>
  bool visit(Graph& g, VisitorQueueHandle vis_queue, AlgData& alg_data) const {
    
    if (!std::get<4>(alg_data)[vertex]) { // TODO: we might not need to maintain this state at all
      return false;
    }

    auto vertex_data = std::get<0>(alg_data)[vertex];
    auto pattern = std::get<1>(alg_data);
    auto pattern_indices = std::get<2>(alg_data);

    // does vertex_data match any entry in the query pattern
    bool match_found = false;
    std::vector<size_t> vertex_pattern_indices(0); // a vertex label could match to multiple pattern labels
    for (size_t vertex_pattern_index = 0; vertex_pattern_index < pattern.size(); vertex_pattern_index++) { 
      if (pattern[vertex_pattern_index] == vertex_data) {
        vertex_pattern_indices.push_back(vertex_pattern_index);
        // TODO: compare with the entry in pattern_indices to detect loop or use token passing
        match_found = true;
        //break;  
      }       
    }

    if (!match_found) {
      //std::get<4>(alg_data)[vertex] = false;
      return false;
    } 

    if (itr_count == 0 && match_found) {
      // send to all the neighbours
      for(eitr_type eitr = g.edges_begin(vertex); 
        eitr != g.edges_end(vertex); ++eitr) {
        vertex_locator neighbor = eitr.target();
       
        for (auto vertex_pattern_index : vertex_pattern_indices) {
          // do this for all the pattern indices for this vertex 
          lppm_visitor new_visitor(neighbor, vertex, vertex_data, vertex_pattern_index, itr_count + 1); 
          vis_queue->queue_visitor(new_visitor); 
        } // for
      } // for
    }

    if ((itr_count >= 1 && itr_count < pattern.size()) && match_found) {
      // heard from a neighbour (parent) whose vertex_data matches an entry in the query pattern
      
      // verify if parent_vertex_data meet constrains for current vertex
      // what pattern label the vartex_data of this vertex corresponds to  
      // what are the valid parent labels for this vertex
      match_found = false; // TODO: Ask Roger about how to set thi visitor property

      for (auto vertex_pattern_index : vertex_pattern_indices) {

      // TODO: move this verification in the update_veretx_state_map function

      // verify index // TODO: this only works for chains, better move to adjacency list representation 
      if (vertex_pattern_index == 0 && 
        !(parent_pattern_index == vertex_pattern_index + 1)) {
        continue;
      } else if (vertex_pattern_index == pattern.size() - 1 && 
        !(parent_pattern_index == vertex_pattern_index - 1)) {
        continue;
      } else if (!(parent_pattern_index == vertex_pattern_index - 1) &&
        !(parent_pattern_index == vertex_pattern_index + 1)) {
        continue;
      }

      // TODO: verify and decide if vertex should reply to the parent and 
      // update vertex_state_map accordingly  
      uint64_t next_itr_count = verify_and_update_vertex_state_map(g, vis_queue, alg_data);
      if (next_itr_count == 0) {
        std ::cout << "."; // Test 
        continue;
      }  

      if (/*itr_count*/next_itr_count >= pattern.size()) { // reached max iterations
        // TODO: Output vertex_ID and pattern_index at the end of nth iteration, similar to updating vertex rank
        // std::get<5>(alg_data)[vertex] // iteration, pattern_id 
        //std::get<3>(alg_data)[vertex]++;
        //return false;
        //match_found = false;
        match_found = true;
        continue;

      } else  {
        match_found = true;
        lppm_visitor new_visitor(parent, vertex, vertex_data, vertex_pattern_index, /*itr_count + 1*/next_itr_count);
        vis_queue->queue_visitor(new_visitor); 
      }  
 
      } // for all vertex patterns 

      if (!match_found) { 
        //std::get<4>(alg_data)[vertex] = false;
        return false;
      }

    } // if
 
    return true;
  }

  friend inline bool operator>(const lppm_visitor& v1, const lppm_visitor& v2) {
    return false;
  }

  friend inline bool operator<(const lppm_visitor& v1, const lppm_visitor& v2) {
    return false;
  }

  template<typename VisitorQueueHandle, typename AlgData>
  uint64_t verify_and_update_vertex_state_map(Graph& g, VisitorQueueHandle vis_queue, AlgData& alg_data) const {
    typedef vertex_state<uint64_t> VertexState;

    // TODO: verify if parent_pattern_index is valid
    auto pattern = std::get<1>(alg_data);   
    auto find_vertex = std::get<5>(alg_data).find(g.locator_to_label(vertex));
    if (find_vertex == std::get<5>(alg_data).end()) {
      auto insert_status = std::get<5>(alg_data).insert({g.locator_to_label(vertex), VertexState()});
      if(!insert_status.second) {
        std::cerr << "Error: failed to add an element to the map." << std::endl;
        return 0;
      }     	
      find_vertex = insert_status.first;
    }

    if (std::get<5>(alg_data).size() < 1) {
      return 0;
    }

    auto find_pattern_vertex =  find_vertex->second.pattern_vertex_itr_count_map.find(parent_pattern_index);  
    if (find_pattern_vertex == find_vertex->second.pattern_vertex_itr_count_map.end()) {
      auto insert_status = find_vertex->second.pattern_vertex_itr_count_map.insert({parent_pattern_index, 0});
      if(!insert_status.second) {
        std::cerr << "Error: failed to add an element to the map." << std::endl;
        return 0;
      }
      find_pattern_vertex = insert_status.first; 
    }      
   
    // update itr_count 
    if (find_pattern_vertex->second < itr_count) {
      find_pattern_vertex->second = itr_count;
    }   

    //if (find_vertex->first == g.locator_to_label(vertex) && g.locator_to_label(vertex) == 38) { // Test

    if (find_vertex->second.pattern_vertex_itr_count_map.size() < 1) {
      return 0;
    }     
    uint64_t min_itr_count = find_vertex->second.pattern_vertex_itr_count_map.begin()->second;

    // TODO: verify if v.first's are valid 
    for (auto& v : find_vertex->second.pattern_vertex_itr_count_map) {
      if (find_vertex->first == g.locator_to_label(vertex) && g.locator_to_label(vertex) == 89) // Test   
        std::cout << "(" << v.first << ", " << v.second << ") "; // Test

      if (v.second < min_itr_count) { // TODO: should be min
        min_itr_count = v.second;  
      } 
    }        
    if (find_vertex->first == g.locator_to_label(vertex) && g.locator_to_label(vertex) == 89) // Test
      std::cout << std::endl; // Test

    if (find_vertex->second.local_itr_count <= min_itr_count) {
      find_vertex->second.local_itr_count = min_itr_count + 1;    
    } 

    if (find_vertex->first == g.locator_to_label(vertex) && g.locator_to_label(vertex) == 89) // Test
      std::cout << " current local_itr_count " << find_vertex->second.local_itr_count << std::endl; // Test   

    //} // Test   
    return find_vertex->second.local_itr_count; 
 
  } 

  vertex_locator vertex;
  vertex_locator parent;
  VertexData parent_vertex_data; 
  size_t parent_pattern_index;
  uint64_t itr_count;
  bool do_update_vertex_pattern_id;
}; 

template <typename TGraph, typename VertexMetaData, typename VertexData, typename PatternData, 
  typename PatternIndices, typename VertexRank, typename VertexActive/*, 
  typename VertexIteration*/, typename VertexStateMap>
void label_propagation_pattern_matching(TGraph* g, VertexMetaData& vertex_metadata, 
  PatternData& pattern, PatternIndices& pattern_indices, VertexRank& vertex_rank,
  VertexActive& vertex_active/*, VertexIteration& vertex_iteration*/, VertexStateMap& vertex_state_map) {
  //std::cout << "label_propagation_pattern_matching.hpp" << std::endl;

  typedef lppm_visitor<TGraph, VertexData> visitor_type;
  auto alg_data = std::forward_as_tuple(vertex_metadata, pattern, pattern_indices, vertex_rank,
    vertex_active/*, vertex_iteration*/, vertex_state_map);
  auto vq = create_visitor_queue<visitor_type, havoqgt::detail::visitor_priority_queue>(g, alg_data);
  vq.init_visitor_traversal_new(); 
  MPI_Barrier(MPI_COMM_WORLD);
  vertex_rank.all_reduce();
  //vertex_iteration.all_reduce(); // TODO: rename to vertex_pattern_id 
  MPI_Barrier(MPI_COMM_WORLD);
}  

}} //end namespace havoqgt::mpi

#endif //HAVOQGT_LABEL_PROPAGATION_PATTERN_MATCHING_HPP_INCLUDED 

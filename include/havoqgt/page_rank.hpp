// Copyright 2013-2020 Lawrence Livermore National Security, LLC and other
// HavoqGT Project Developers. See the top-level LICENSE file for details.
//
// SPDX-License-Identifier: MIT
 

#ifndef HAVOQGT_MPI_PAGE_RANK_HPP_INCLUDED
#define HAVOQGT_MPI_PAGE_RANK_HPP_INCLUDED


#include <limits>
#include <havoqgt/visitor_queue.hpp>
#include <boost/container/deque.hpp>
#include <vector>

namespace havoqgt {

template <typename Visitor>
class pr_queue
{

protected:
  std::vector< Visitor > m_data;
public:
  pr_queue() { }

  bool push(Visitor const & task)
  {
    m_data.push_back(task);
    return true;
  }

  void pop()
  {
    m_data.pop_back();
  }

  Visitor const & top() //const
  {
    return m_data.back();
  }

  size_t size() const
  {
    return m_data.size();;
  }

  bool empty() const
  {
    return m_data.empty();
  }

  void clear()
  {
    m_data.clear();
  }
};



template<typename Graph>
class pr_visitor {
public:
  typedef typename Graph::vertex_locator                 vertex_locator;
  pr_visitor(): rank(std::numeric_limits<double>::min())  { }

  pr_visitor(vertex_locator _vertex, double _rank)
    : vertex(_vertex)
    , rank(_rank) { }

  pr_visitor(vertex_locator _vertex)
    : vertex(_vertex)
    , rank(std::numeric_limits<double>::min()) { }      

  template<typename AlgData> 
  bool pre_visit(AlgData& alg_data) const {
    if(rank == std::numeric_limits<double>::min()) {
      HAVOQGT_ERROR_MSG("This is a damn logic error!");
      return true;
    }
    std::get<1>(alg_data)[vertex] += rank; //change to next_rank 
    return false;
  }
  
  template<typename VisitorQueueHandle, typename AlgData>
  bool init_visit(Graph& g, VisitorQueueHandle vis_queue, AlgData& alg_data) const {
    return visit(g, vis_queue, alg_data);
  }

  template<typename VisitorQueueHandle, typename AlgData>
  bool visit(Graph& g, VisitorQueueHandle vis_queue, AlgData& alg_data) const {
    //change to cur_rank
    double old_rank = std::get<0>(alg_data)[vertex];    
    uint64_t degree = g.degree(vertex);
    double send_rank = old_rank / double(degree);


    typedef typename Graph::edge_iterator eitr_type;
    for(eitr_type eitr = g.edges_begin(vertex); eitr != g.edges_end(vertex); ++eitr) {
      vertex_locator neighbor = eitr.target();
      pr_visitor new_visitor( neighbor, send_rank);
      vis_queue->queue_visitor(new_visitor);
    }
    return true;
  }


  friend inline bool operator>(const pr_visitor& v1, const pr_visitor& v2) {
    return false;
  }

  friend inline bool operator<(const pr_visitor& v1, const pr_visitor& v2) {
    return false;
  }

  vertex_locator   vertex;
  double           rank;
};

template <typename TGraph, typename PRData>
void page_rank(TGraph& g, PRData& cur_rank, PRData& next_rank, bool initial) {
  typedef  pr_visitor<TGraph>    visitor_type;
  auto alg_data = std::forward_as_tuple(cur_rank, next_rank);
   
  if(initial) {
    cur_rank.reset(double(1)/double(g.max_global_vertex_id()));
  }
   
  auto vq = create_visitor_queue<visitor_type, detail::visitor_priority_queue>(&g, alg_data);
  vq.init_visitor_traversal();
  next_rank.all_reduce();
}



} //end namespace havoqgt




#endif //HAVOQGT_MPI_PAGE_RANK_HPP_INCLUDED

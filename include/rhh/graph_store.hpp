/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Roger Pearce <rpearce@llnl.gov>.
 * LLNL-CODE-644630.
 * All rights reserved.
 *
 * This file is part of HavoqGT, Version 0.1.
 * For details, see
 * https://computation.llnl.gov/casc/dcca-pub/dcca/Downloads.html
 *
 * Please also read this link – Our Notice and GNU Lesser General Public
 * License. http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License (as published by the Free
 * Software Foundation) version 2.1 dated February 1999.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the terms and conditions of the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * OUR NOTICE AND TERMS AND CONDITIONS OF THE GNU GENERAL PUBLIC LICENSE
 *
 * Our Preamble Notice
 *
 * A. This notice is required to be provided under our contract with the
 * U.S. Department of Energy (DOE). This work was produced at the Lawrence
 * Livermore National Laboratory under Contract No. DE-AC52-07NA27344 with the
 * DOE.
 *
 * B. Neither the United States Government nor Lawrence Livermore National
 * Security, LLC nor any of their employees, makes any warranty, express or
 * implied, or assumes any liability or responsibility for the accuracy,
 * completeness, or usefulness of any information, apparatus, product, or
 * process disclosed, or represents that its use would not infringe
 * privately-owned rights.
 *
 * C. Also, reference herein to any specific commercial products, process, or
 * services by trade name, trademark, manufacturer or otherwise does not
 * necessarily constitute or imply its endorsement, recommendation, or favoring
 * by the United States Government or Lawrence Livermore National Security, LLC.
 * The views and opinions of authors expressed herein do not necessarily state
 * or reflect those of the United States Government or Lawrence Livermore
 * National Security, LLC, and shall not be used for advertising or product
 * endorsement purposes.
 *
 */

#ifndef HAVOQGT_INCLUDE_RHH_DYNAMIC_GRAPH_STORE_HPP
#define HAVOQGT_INCLUDE_RHH_DYNAMIC_GRAPH_STORE_HPP

#include <memory>
#include <utility>

#include <rhh/hash.hpp>
#include <rhh/rhh_map.hpp>
#include <rhh/detail/rhh_const_key_iterator.hpp>

namespace rhh {

template <typename _vertex_type, typename _vertex_value_type,
          typename _edge_value_type,
          typename _allocator_type = std::allocator<std::byte>>
class graph_store {
 public:
  using vertex_type       = _vertex_type;
  using vertex_value_type = _vertex_value_type;
  using edge_value_type   = _edge_value_type;
  using allocator_type    = _allocator_type;

 private:
  using edge_list_t = rhh_map<vertex_type, edge_value_type, hash<vertex_type>,
                              std::equal_to<vertex_type>, allocator_type>;

  using adjacencly_list_t = rhh_map<vertex_type, edge_list_t, hash<vertex_type>,
                                    std::equal_to<vertex_type>, allocator_type>;

  using vertex_table_t =
      rhh_map<vertex_type, vertex_value_type, hash<vertex_type>,
              std::equal_to<vertex_type>, allocator_type>;

 public:
  using vertex_iterator     = detail::rhh_key_iterator<adjacencly_list_t>;
  using edge_iterator       = detail::rhh_key_iterator<edge_list_t>;

  explicit graph_store(const allocator_type &alloc = allocator_type())
      : m_adj_list(alloc), m_vertex_table(alloc) {}

  graph_store(const graph_store &)     = default;
  graph_store(graph_store &&) noexcept = default;

  ~graph_store() = default;

  graph_store &operator=(const graph_store &) = default;
  graph_store &operator=(graph_store &&) noexcept = default;

  bool has_vertex(const vertex_type &vertex) const {
    return m_vertex_table.find(vertex) != adjacencly_list_t::npos;
  }

  bool insert_vertex(
      const vertex_type &      vertex,
      const vertex_value_type &vertex_value = vertex_value_type()) {
    if (has_vertex(vertex)) return false;
    m_vertex_table.insert(std::make_tuple(vertex, vertex_value));
    return true;
  }

  bool insert_vertex(vertex_type &&    vertex,
                     vertex_value_type vertex_value = vertex_value_type()) {
    if (has_vertex(vertex)) return false;
    m_vertex_table.insert(
        std::make_tuple(std::move(vertex), std::move(vertex_value)));
    return true;
  }

  void insert_edge(const vertex_type &    source_vertex,
                   const vertex_type &    target_vertex,
                   const edge_value_type &edge_value = edge_value_type()) {
    auto src_pos = m_adj_list.find(source_vertex);
    if (src_pos == m_adj_list.npos) {
      src_pos = m_adj_list.insert_key(source_vertex);
      assert(src_pos != adjacencly_list_t::npos);
    }
    auto &edge_list = std::get<1>(m_adj_list.at(src_pos));
    edge_list.insert(std::make_tuple(target_vertex, edge_value));
  }

  void insert_edge(vertex_type &&source_vertex, vertex_type &&target_vertex,
                   edge_value_type edge_value = edge_value_type()) {
    auto src_pos = m_adj_list.find(source_vertex);
    if (src_pos == adjacencly_list_t::npos) {
      src_pos = m_adj_list.insert_key(std::move(source_vertex));
    }
    auto &edge_list = std::get<1>(m_adj_list.at(src_pos));
    edge_list.insert(
        std::make_tuple(std::move(target_vertex), std::move(edge_value)));
  }

  std::size_t num_vertices() const { return m_adj_list.size(); }

  std::size_t degree(const vertex_type &vertex) const {
    const auto position = m_adj_list.find(vertex);
    if (position == adjacencly_list_t::npos) return 0;

    return std::get<1>(m_adj_list.at(position)).size();
  }

  vertex_value_type &vertex_value(const vertex_type &vertex) {
    const auto pos = m_vertex_table.find(vertex);
    assert(pos != vertex_table_t::npos);
    return std::get<1>(m_vertex_table.at(pos));
  }

  const vertex_value_type &vertex_value(const vertex_type &vertex) const {
    const auto pos = m_vertex_table.find(vertex);
    assert(pos != vertex_table_t::npos);
    return std::get<1>(m_vertex_table.at(pos));
  }

  edge_value_type &edge_value(const vertex_type &source,
                              const vertex_type &target) {
    const auto src_pos = m_adj_list.find(source);
    assert(src_pos != adjacencly_list_t::npos);
    auto &     edge_list = std::get<1>(m_adj_list.at(src_pos));
    const auto dest_pos   = edge_list.find(target);
    assert(dest_pos != edge_list_t::npos);
    return std::get<1>(edge_list.at(dest_pos));
  }

  const edge_value_type &edge_value(const vertex_type &source,
                                    const vertex_type &target) const {
    const auto src_pos = m_adj_list.find(source);
    assert(src_pos != adjacencly_list_t::npos);
    auto &     edge_list = std::get<1>(m_adj_list.at(src_pos));
    const auto dest_pos   = edge_list.find(target);
    assert(dest_pos != edge_list_t::npos);
    return std::get<1>(edge_list.at(dest_pos));
  }

  vertex_iterator vertices_begin() const {
    return vertex_iterator(&m_adj_list, 0);
  }

  vertex_iterator vertices_end() const {
    auto itr = vertex_iterator(&m_adj_list);
    itr.move_to_end();
    return itr;
  }

  edge_iterator edges_begin(const vertex_iterator& source_vertex_iterator) const {
    const auto& edge_list = std::get<1>(m_adj_list.at(source_vertex_iterator.position()));
    return edge_iterator(&edge_list, 0);
  }

  edge_iterator edges_end(const vertex_iterator& source_vertex_iterator) const {
    const auto& edge_list = std::get<1>(m_adj_list.at(source_vertex_iterator.position()));
    auto itr = edge_iterator(&edge_list);
    itr.move_to_end();
    return itr;
  }

 private:
  adjacencly_list_t m_adj_list;
  vertex_table_t    m_vertex_table;
};

}  // namespace rhh
#endif  // HAVOQGT_INCLUDE_RHH_DYNAMIC_GRAPH_STORE_HPP

/*
 * Written by Keita Iwabuchi.
 * LLNL / TokyoTech
 */

#ifndef DYNAMICGRAPHSTORE_BENCH_HPP
#define DYNAMICGRAPHSTORE_BENCH_HPP

#include <iostream>
#include <utility>
#include <algorithm>
#include <functional>

#ifdef __linux__
#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif
  #include <fcntl.h>
#endif

#include <boost/interprocess/managed_mapped_file.hpp>

#include <havoqgt/rmat_edge_generator.hpp>
#include <havoqgt/environment.hpp>
#include <havoqgt/parallel_edge_list_reader.hpp>

#include <havoqgt/graphstore/graphstore_utilities.hpp>
#include <havoqgt/graphstore/graphstore_common.hpp>

#define VERBOSE 1

#define DEBUG_MODE 0
#if DEBUG_MODE
std::ofstream ofs_edges;
#endif

#define SORT_BY_CHUNK 0

/// --- typenames --- ///
using mapped_file_type     = boost::interprocess::managed_mapped_file;
using segment_manager_type = boost::interprocess::managed_mapped_file::segment_manager;


/// --------------------------- edge update request ----------------------------------- ///
template<typename vertex_id_type>
struct edge_update_request
{
  edge_update_request()
  { }

  edge_update_request(const std::pair<vertex_id_type, vertex_id_type>& _edge, const bool _is_delete) :
    edge(_edge),
    is_delete(_is_delete)
  { }

  std::pair<vertex_id_type, vertex_id_type> edge;
  bool is_delete;
};

template<typename vertex_id_type>
edge_update_request<vertex_id_type> make_update_request(const std::pair<vertex_id_type, vertex_id_type>& pair)
{
  return edge_update_request<vertex_id_type>(std::make_pair(std::get<0>(pair), std::get<1>(pair)),
                                           false);
}

template<typename vertex_id_type>
edge_update_request<vertex_id_type> make_update_request(const std::tuple<vertex_id_type, vertex_id_type, bool>& pair)
{
  return edge_update_request<vertex_id_type>(std::make_pair(std::get<0>(pair), std::get<1>(pair)),
                                           std::get<2>(pair));
}


template<typename vertex_id_type>
bool edgerequest_asc(const edge_update_request<vertex_id_type>& left, const edge_update_request<vertex_id_type>& right ) {
  return left.edge.first < right.edge.first;
}
template<typename vertex_id_type>
using request_vector_type = std::vector<edge_update_request<vertex_id_type>>;


template<typename vertex_id_type>
double sort_requests(request_vector_type<vertex_id_type>& requests)
{
  const double time_start1 = MPI_Wtime();
  std::sort(requests.begin(), requests.end(), edgerequest_asc);
  return (MPI_Wtime() - time_start1);
}


/// --------------------------- generate edge update requests ----------------------------------- ///
template <typename edgelist_itr_type, typename vertex_id_type>
std::pair<vertex_id_type, size_t> generate_update_requests(edgelist_itr_type& edgelist_itr,
                                                              edgelist_itr_type& edgelist_itr_last,
                                                              request_vector_type<vertex_id_type>& requests,
                                                              size_t max_size)
{
  const int mpi_rank = havoqgt::havoqgt_env()->world_comm().rank();

  vertex_id_type max_vertex_id = 0;
  size_t cnt = 0;
  const double time_start = MPI_Wtime();
  requests.clear();
  while (edgelist_itr != edgelist_itr_last) {
    requests.push_back(make_update_request(*edgelist_itr));
    max_vertex_id = std::max(max_vertex_id, std::get<0>(*edgelist_itr));
    max_vertex_id = std::max(max_vertex_id, std::get<1>(*edgelist_itr));
    ++edgelist_itr;
    ++cnt;
    if (max_size <= cnt) break;
  }
  if (max_size != cnt)
    requests.resize(cnt);
  havoqgt::havoqgt_env()->world_comm().barrier();

  if (mpi_rank == 0) std::cout << "generated edges into DRAM (sec.) =\t" << MPI_Wtime() - time_start << std::endl;
  havoqgt::havoqgt_env()->world_comm().barrier();

  std::cout << "[" << mpi_rank << "] # of generated insetion requests =\t"<< requests.size() << std::endl;

#if SORT_BY_CHUNK
  const double elapsed_time = sort_requests(requests);
  havoqgt::havoqgt_env()->world_comm().barrier();
  if (mpi_rank == 0) std::cout << "TIME: Sorting chunk (sec.) =\t" << elapsed_time << std::endl;
 #endif

  return std::make_pair(max_vertex_id, cnt);
}


/// --------------------------- utilities for system call ----------------------------------- ///
void print_system_mem_usages()
{
  size_t mem_unit, totalram, freeram, usedram, bufferram, totalswap, freeswap;
  if (graphstore::utility::get_system_memory_usages(&mem_unit, &totalram, &freeram, &usedram, &bufferram, &totalswap, &freeswap)) {
//    std::cout << "Usage: mem_unit:\t" << mem_unit << "\n";
//    std::cout << "Usage: totalram(GiB):\t" << static_cast<double>(totalram) / (1<<30ULL) << "\n";
    std::cout << "Usage: freeram(GiB):\t" << static_cast<double>(freeram) / (1<<30ULL) << "\n";
    std::cout << "Usage: usedram(GiB):\t" << static_cast<double>(usedram) / (1<<30ULL) << "\n";
//    std::cout << "Usage: bufferram(GiB):\t" << static_cast<double>(bufferram) / (1<<30ULL) << "\n";
//    std::cout << "Usage: totalswap(GiB):\t" << static_cast<double>(totalswap) / (1<<30ULL) << "\n";
//    std::cout << "Usage: freeswap(GiB):\t" << static_cast<double>(freeswap) / (1<<30ULL) << "\n";
//    std::cout << "----------------------------" << std::endl;
  }
//  size_t size, resident, share, text, lib, data, dt;
//  if (graphstore::utility::get_my_memory_usages(&size, &resident, &share, &text, &lib, &data, &dt)) {
//    std::cout << "Usage: VmSize(GiB):\t" << static_cast<double>(size) / (1<<30ULL) << "\n";
//    std::cout << "Usage: VmRSS(GiB):\t" << static_cast<double>(resident) / (1<<30ULL) << "\n";
//    std::cout << "Usage: sharedpages(GiB):\t" << static_cast<double>(share) / (1<<30ULL) << "\n";
//    std::cout << "Usage: text(GiB):\t" << static_cast<double>(text) / (1<<30ULL) << "\n";
//    std::cout << "Usage: library(GiB):\t" << static_cast<double>(lib) / (1<<30ULL) << "\n";
//    std::cout << "Usage: data+stack(GiB):\t" << static_cast<double>(data) / (1<<30ULL) << "\n";
//    std::cout << "Usage: dirtypages(GiB):\t" << static_cast<double>(dt) / (1<<30ULL) << "\n";
//    std::cout << "----------------------------" << std::endl;
//  }

  size_t dirty;
  if (graphstore::utility::get_meminfo("Dirty", dirty)) {
    std::cout << "Usage: Dirty(KB):\t" << dirty << "\n";
  }
}


void fallocate(const char* const fname, size_t size, mapped_file_type& asdf)
{
#ifdef __linux__
    std::cout << "Call fallocate()" << std::endl;
    int fd  = open(fname, O_RDWR);
    assert(fd != -1);
    /// posix_fallocate dosen't work on XFS ?
    /// (dosen't actually expand the file size ?)
    int ret = fallocate(fd, 0, 0, size);
    assert(ret == 0);
    close(fd);
    asdf.flush();
#else
#warning fallocate() is not supported
#endif
}

void flush_mmmap(mapped_file_type& mapped_file)
{
  std::cout << "flush mmap" << std::endl;
  mapped_file.flush();
}

void sync_mmap()
{
#if _BSD_SOURCE || _XOPEN_SOURCE >= 500
  std::cout << "sync mmap: sync(2)" << std::endl;
  sync();
#else
#warning sync(2) is not supported
#endif
}


/// --------------------------- utilities for interprocess ----------------------------------- ///
double get_segment_size(segment_manager_type *const segment_manager)
{
  const size_t usages = segment_manager->get_size() - segment_manager->get_free_memory();
  return static_cast<double>(usages) / (1ULL << 30);
}

void interprocess_mapped_file_dontneed(mapped_file_type& mapped_file)
{
  std::cout << "Call adise_dontneed" << std::endl;
  boost::interprocess::mapped_region::advice_types advise = boost::interprocess::mapped_region::advice_types::advice_dontneed;
  assert(false);
  /// assert(mapped_file.advise(advise));
}


void segment_manager_zero_free_memory(segment_manager_type& segment_manager, mapped_file_type& mapped_file)
{
    std::cout << "Call segment_manager.zero_free_memory()" << std::endl;
    segment_manager.zero_free_memory();
    std::cout << "Call mapped_file.flush()" << std::endl;
    mapped_file.flush();
    std::cout << "Call sync" << std::endl;
    sync_mmap();
}

void interprocess_delete_segmentfile(const char* fname)
{
  boost::interprocess::file_mapping::remove(fname);
}

/// --------------------------- utilieies for DI-MMAP ----------------------------------- ///
void flush_dimmap()
{
  std::cout << "flush di-mmap" << std::endl;
  std::ofstream flusher("/sys/class/di-mmap-runtimeA/flush_buffer");
  flusher << "1" << std::endl;
}

void sync_dimmap()
{
  std::cout << "sync di-mmap" << std::endl;
  std::ofstream flusher("/proc/di-mmap-runtimeA-tuning");
  flusher << "mmap_sync_buffers" << std::endl;
}


#endif // DYNAMICGRAPHSTORE_BENCH_HPP

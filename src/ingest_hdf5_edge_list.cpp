// Copyright 2013-2020 Lawrence Livermore National Security, LLC and other
// HavoqGT Project Developers. See the top-level LICENSE file for details.
//
// SPDX-License-Identifier: MIT

#include <havoqgt/delegate_partitioned_graph.hpp>
#include <havoqgt/hdf5_edge_list_reader.hpp>
#include <havoqgt/distributed_db.hpp>

#include <iostream>
#include <assert.h>
#include <functional>
#include <unistd.h>
#include <memory>

// notes for how to setup a good test
// take rank * 100 and make edges between (all local)
// Make one vert per rank a hub.

using namespace havoqgt;

typedef delegate_partitioned_graph<distributed_db::allocator<>> graph_type;
typedef hdf5_edge_list_reader::weight_type weight_type;
typedef distributed_db::allocator<weight_type> edge_data_allocator_type;
typedef graph_type::edge_data<weight_type, edge_data_allocator_type> edge_data_type;

constexpr const char *k_edge_data_name = "graph_edge_data_obj";

void usage()  {
  if(comm_world().rank() == 0) {
    std::cerr << "Usage: -o <string> -d <int> [file ...]\n"
              << " -o <string>   - output graph base filename (required)\n"
              << " -s <string>   - key name of source vertex list (required)\n"
              << " -t <string>   - key name of destination vertex list (required)\n"
              << " -w <string>   - key name of edge weight list\n"
              << " -b <string>   - backup graph base filename \n"
              << " -d <int>      - delegate threshold (Default is 1048576)\n"
              << " -h            - print help and exit\n"
              << " -p <int>      - number of Low & High partition passes (Default is 1)\n"
              << " -f <float>    - Gigabytes reserved per rank (Default is 0.25)\n"
              << " -c <int>      - Edge partitioning chunk size (Defulat is 8192)\n"
              << " -u <bool>     - Treat edgelist as undirected (Default is 0)\n"
              << "[file ...] - list of edge list files to ingest\n\n";
  }
}

void parse_cmd_line(int argc, char** argv, std::string& output_filename, std::string& backup_filename,
                    uint64_t& delegate_threshold, std::vector< std::string >& input_filenames,
                    double& gbyte_per_rank, uint64_t& partition_passes, uint64_t& chunk_size,
                    bool& undirected, std::string &src_key, std::string &dst_key, std::string& weight_key) {
  if(comm_world().rank() == 0) {
    std::cout << "CMD line:";
    for (int i=0; i<argc; ++i) {
      std::cout << " " << argv[i];
    }
    std::cout << std::endl;
  }

  bool found_output_filename = false;
  delegate_threshold = 1048576;
  input_filenames.clear();
  gbyte_per_rank = 0.25;
  partition_passes = 1;
  chunk_size = 8*1024;
  undirected = false;

  int c;
  bool prn_help = false;
  while ((c = getopt(argc, argv, "o:d:p:f:c:b:u:hs:t:w:")) != -1) {
    switch (c) {
      case 'h':
        prn_help = true;
        break;
      case 'd':
        delegate_threshold = atoll(optarg);
        break;
      case 'o':
        found_output_filename = true;
        output_filename = optarg;
        break;
      case 'b':
        backup_filename = optarg;
        break;
      case 'p':
        partition_passes = atoll(optarg);
        break;
      case 'f':
        gbyte_per_rank = atof(optarg);
        break;
      case 'c':
        chunk_size = atoll(optarg);
        break;
      case 'u':
        undirected = atoi(optarg);
        break;
      case 's':
        src_key = optarg;
        break;
      case 't':
        dst_key = optarg;
        break;
      case 'w':
        weight_key = optarg;
        break;
      default:
        std::cerr << "Unrecognized option: "<<c<<", ignore."<<std::endl;
        prn_help = true;
        break;
    }
  }
  if (prn_help || !found_output_filename || src_key.empty() || dst_key.empty()) {
    usage();
    exit(-1);
  }

  for (int index = optind; index < argc; index++) {
    ///std::cout << "Input file = " << argv[index] << std::endl;
    input_filenames.push_back(argv[index]);
  }
}

std::vector<std::string> local_assign_inputs(const std::vector<std::string>& all_filenames) {
  const int shm_rank  = comm_nl().rank();
  const int shm_size  = comm_nl().size();
  const int node_rank = comm_nr().rank();
  const int node_size = comm_nr().size();

  std::vector<std::string> sub_filenames;
  for (std::size_t i = 0; i < all_filenames.size(); ++i) {
    if (i % node_size == node_rank && (i / node_size) % shm_size == shm_rank) {
      sub_filenames.push_back(all_filenames[i]);
    }
  }
}

auto read_edge(const std::vector<std::string> &input_filenames,
               const std::string &src_key,
               const std::string &dst_key,
               const std::string &weight_key,
               const bool undirected) {

  std::vector<std::string> assigned_filenames = local_assign_inputs(input_filenames);

  std::vector<std::tuple<uint64_t, uint64_t, weight_type>> edge_list;
  for (const auto& file_name : assigned_filenames) {
    hdf5_edge_list_reader reader;
    if (!reader.read(file_name, src_key, dst_key, weight_key)) {
      std::cerr << "Failed to read edge: " << file_name << std::endl;
    }

    const auto& read_edge_lists = reader.edges();
    assert(std::get<0>(read_edge_lists).size() ==
           std::get<1>(read_edge_lists).size());
    for (std::size_t e = 0; e < std::get<0>(read_edge_lists).size(); ++e) {
      const auto src = std::get<0>(read_edge_lists)[e];
      const auto dst = std::get<1>(read_edge_lists)[e];
      const auto weight = weight_key.empty() ? weight_type()
                                             : std::get<2>(read_edge_lists)[e];
      edge_list.emplace_back(src, dst, weight);
      if (undirected) edge_list.emplace_back(dst, src, weight);
    }
  }

  return edge_list;
}

uint64_t find_global_max_vertex_id(const std::vector<std::tuple<uint64_t, uint64_t, weight_type>> &edge_list) {
  uint64_t local_max_vertex = 0;
  for (const auto &edge : edge_list) {
    local_max_vertex = std::max(std::get<0>(edge), local_max_vertex);
    local_max_vertex = std::max(std::get<1>(edge), local_max_vertex);
  }

  const auto global_max_vertex = mpi_all_reduce(local_max_vertex, std::greater<uint64_t>(), MPI_COMM_WORLD);

  return global_max_vertex;
}

int main(int argc, char** argv) {

  init(&argc, &argv);
  {
    std::string                output_filename;
    std::string                backup_filename;

    { // Build Distributed_DB
      const int mpi_size = comm_world().size();

      cout_rank0_barrier() << "MPI initialized with " << mpi_size << " ranks." << std::endl;

      uint64_t                   delegate_threshold;
      std::vector< std::string > input_filenames;
      uint64_t                   partition_passes;
      double                     gbyte_per_rank;
      uint64_t                   chunk_size;
      bool                       undirected;
      std::string src_key;
      std::string dst_key;
      std::string weight_key;

      parse_cmd_line(argc, argv, output_filename, backup_filename,
                     delegate_threshold, input_filenames, gbyte_per_rank,
                     partition_passes, chunk_size, undirected, src_key, dst_key,
                     weight_key);

      cout_rank0_barrier() << "Ingesting graph from " << input_filenames.size() << " files." << std::endl;

      distributed_db ddb(db_create(), output_filename.c_str());
      auto* edge_data_ptr = ddb.get_manager()->construct<edge_data_type>(k_edge_data_name)(ddb.get_allocator());

      const auto edge_list = read_edge(input_filenames, src_key, dst_key, weight_key, undirected);
      const auto max_vertex_id = find_global_max_vertex_id(edge_list);

      cout_rank0_barrier() << "Generating new graph." << std::endl;
      graph_type *graph = ddb.get_manager()->construct<graph_type>
          ("graph_obj")
          (ddb.get_allocator(), MPI_COMM_WORLD,edge_list, max_vertex_id, delegate_threshold, partition_passes, chunk_size, *edge_data_ptr);

      if (weight_key.empty()) {
        ddb.get_manager()->destroy<edge_data_type>(k_edge_data_name);
        edge_data_ptr = nullptr;
      }

      cout_rank0_barrier() << "Graph Ready." << std::endl;
    } // Complete build distributed_db

    if(backup_filename.size() > 0) {
      distributed_db::transfer(output_filename.c_str(), backup_filename.c_str());
      cout_rank0_barrier() << "Created Backup" << std::endl;
    }
  } //END Main MPI

  return 0;
}

/*
 * Copyright (c) 2013, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 * Written by Roger Pearce <rpearce@llnl.gov>.
 * LLNL-CODE-644630.
 * All rights reserved.
 *
 * This file is part of HavoqGT, Version 0.1.
 * For details, see https://computation.llnl.gov/casc/dcca-pub/dcca/Downloads.html
 *
 * Please also read this link – Our Notice and GNU Lesser General Public License.
 *   http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License (as published by the Free
 * Software Foundation) version 2.1 dated February 1999.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the terms and conditions of the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * OUR NOTICE AND TERMS AND CONDITIONS OF THE GNU GENERAL PUBLIC LICENSE
 *
 * Our Preamble Notice
 *
 * A. This notice is required to be provided under our contract with the
 * U.S. Department of Energy (DOE). This work was produced at the Lawrence
 * Livermore National Laboratory under Contract No. DE-AC52-07NA27344 with the DOE.
 *
 * B. Neither the United States Government nor Lawrence Livermore National
 * Security, LLC nor any of their employees, makes any warranty, express or
 * implied, or assumes any liability or responsibility for the accuracy,
 * completeness, or usefulness of any information, apparatus, product, or process
 * disclosed, or represents that its use would not infringe privately-owned rights.
 *
 * C. Also, reference herein to any specific commercial products, process, or
 * services by trade name, trademark, manufacturer or otherwise does not
 * necessarily constitute or imply its endorsement, recommendation, or favoring by
 * the United States Government or Lawrence Livermore National Security, LLC. The
 * views and opinions of authors expressed herein do not necessarily state or
 * reflect those of the United States Government or Lawrence Livermore National
 * Security, LLC, and shall not be used for advertising or product endorsement
 * purposes.
 *
 */


#include <havoqgt/cache_utilities.hpp>
#include <havoqgt/connected_components.hpp>
#include <havoqgt/delegate_partitioned_graph.hpp>

#include <boost/bind.hpp>
#include <boost/function.hpp>

#include <havoqgt/distributed_db.hpp>
#include <assert.h>

#include <deque>
#include <string>
#include <utility>
#include <algorithm>
#include <functional>

#include <boost/interprocess/managed_heap_memory.hpp>

using namespace havoqgt;

void usage()  {
  if(comm_world().rank() == 0) {
    std::cerr << "Usage: -i <string> -s <int>\n"
         << " -i <string>   - input graph base filename (required)\n"
         << " -b <string>   - backup graph base filename.  If set, \"input\" graph will be deleted if it exists\n"
         << " -h            - print help and exit\n\n";
  }
}

void parse_cmd_line(int argc, char** argv, std::string& input_filename, std::string& backup_filename) {
  if(comm_world().rank() == 0) {
    std::cout << "CMD line:";
    for (int i=0; i<argc; ++i) {
      std::cout << " " << argv[i];
    }
    std::cout << std::endl;
  }
  
  bool found_input_filename = false;
  
  char c;
  bool prn_help = false;
  while ((c = getopt(argc, argv, "i:b:h ")) != -1) {
     switch (c) {
       case 'h':  
         prn_help = true;
         break;
      case 'i':
         found_input_filename = true;
         input_filename = optarg;
         break;
      case 'b':
         backup_filename = optarg;
         break;
      default:
         std::cerr << "Unrecognized option: "<<c<<", ignore."<<std::endl;
         prn_help = true;
         break;
     }
   } 
   if (prn_help || !found_input_filename) {
     usage();
     exit(-1);
   }
}

int main(int argc, char** argv) {
  typedef delegate_partitioned_graph<distributed_db::allocator<>> graph_type;

  int mpi_rank(0), mpi_size(0);

  havoqgt::init(&argc, &argv);
  {
  CHK_MPI(MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank));
  CHK_MPI(MPI_Comm_size(MPI_COMM_WORLD, &mpi_size));
  
  if (mpi_rank == 0) {
    std::cout << "MPI initialized with " << mpi_size << " ranks." << std::endl;
    //print_system_info(false);
  }
  MPI_Barrier(MPI_COMM_WORLD);


  std::string graph_input;
  std::string backup_filename;
  
  parse_cmd_line(argc, argv, graph_input, backup_filename);


  MPI_Barrier(MPI_COMM_WORLD);
  if(backup_filename.size() > 0) {
    distributed_db::transfer(backup_filename.c_str(), graph_input.c_str());
  }

  distributed_db ddb(db_open_read_only(), graph_input.c_str());

  auto graph = ddb.get_manager()->find<graph_type>("graph_obj").first;
  assert(graph != nullptr);

  MPI_Barrier(MPI_COMM_WORLD);
  if (mpi_rank == 0) {
    std::cout << "Graph Loaded Ready." << std::endl;
  }
  //graph->print_graph_statistics();
  MPI_Barrier(MPI_COMM_WORLD);
 
  graph_type::vertex_data<graph_type::vertex_locator, std::allocator<graph_type::vertex_locator> >  cc_data(*graph);

    MPI_Barrier(MPI_COMM_WORLD);
    double time_start = MPI_Wtime();
    connected_components(graph, cc_data);
    MPI_Barrier(MPI_COMM_WORLD);
    double time_end = MPI_Wtime();
    
    // std::map<uint64_t, uint64_t> cc_count;
    // for(auto vitr=graph->vertices_begin(); vitr != graph->vertices_end(); ++vitr) {
    //   cc_count[graph->locator_to_label(cc_data[*vitr])]++;
    // }
    // auto cc_count_itr = cc_count.begin();
    // uint64_t largest_cc = 0;
    // uint64_t num_ccs = 0;
    // while(!mpi::detail::global_iterator_range_empty(cc_count_itr, cc_count.end(), MPI_COMM_WORLD)) {
    //   uint64_t local_next_cc = cc_count_itr->first;
    //   uint64_t global_next_cc = mpi_all_reduce(local_next_cc, std::less<uint64_t>(), MPI_COMM_WORLD);
    //   uint64_t local_count = (local_next_cc == global_next_cc) ?  (cc_count_itr++)->second : 0;
    //   uint64_t global_cc_count = mpi_all_reduce(local_count, std::plus<uint64_t>(), MPI_COMM_WORLD);
    //   //if(mpi_rank == 0 ) {
    //   //  std::cout << "CC " << global_next_cc << ", size = " << global_cc_count << std::endl;
    //   //}
    //   largest_cc = std::max(global_cc_count, largest_cc);
    //   num_ccs ++;
    // }
    if(mpi_rank == 0){
      //std::cout << "Num CCs = " << num_ccs << ", largest CC = " << largest_cc << ", Traversal Time = " << time_end - time_start << std::endl;
      std::cout << "Traversal Time = " << time_end - time_start << std::endl;
    }
  }  // END Main MPI
  ;

  return 0;
}

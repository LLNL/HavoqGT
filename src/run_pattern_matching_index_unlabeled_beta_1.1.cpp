#include <bitset>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/interprocess/managed_heap_memory.hpp>

#include <havoqgt/cache_utilities.hpp>
#include <havoqgt/delegate_partitioned_graph.hpp>
#include <havoqgt/distributed_db.hpp>
///#include <havoqgt/environment.hpp>

#include <metadata/vertex_data_db.hpp>
#include <metadata/vertex_data_db_degree.hpp>
#include <metadata/vertex_data_db_same_label.hpp>

#include <prunejuice/template.hpp>
#include <prunejuice/non_local_constraint.hpp>
#include <prunejuice/algorithm_state.hpp>
#include <prunejuice/local_constraint_checking.hpp>
#include <prunejuice/local_constraint_checking_any_neighbor.hpp>
#include <prunejuice/non_local_constraint_checking_unique.hpp>
#include <prunejuice/non_local_constraint_checking_tds_batch.hpp> // TODO: optimize for batch_size = mpi_size
//#include <prunejuice/non_local_constraint_checking_tds_jump_batch.hpp> // TODO: optimize for batch_size = mpi_size

// indexing
//#include <prunejuice/local_constraint_checking_3clique.hpp>
//#include <prunejuice/local_constraint_checking_3clique_2.hpp>
#include <prunejuice/local_constraint_checking_unlabeled_3clique.hpp>
//#include <prunejuice/non_local_constraint_checking_tds_batch_edge_state_unlabeled_3clique.hpp>
#include <prunejuice/non_local_constraint_checking_tds_batch_edge_state_unlabeled_3clique_2.hpp>
#include <prunejuice/three_clique_edge_state_unlabeled_batch.hpp>
//#include <prunejuice/synchronize_edge_state_unlabeled_batch.hpp>
#include <prunejuice/synchronize_edge_state_unlabeled_batch_2.hpp>
#include <prunejuice/ktruss.hpp>
//#include <prunejuice/kclique.hpp>
#include <prunejuice/kclique_2.hpp>

/*
//#include <havoqgt/graph.hpp>
#include <havoqgt/pattern_graph.hpp>
#include <havoqgt/pattern_util.hpp>
//#include <havoqgt/label_propagation_pattern_matching.hpp>
//#include <havoqgt/label_propagation_pattern_matching_bsp.hpp> 
//#include <havoqgt/label_propagation_pattern_matching_iterative.hpp>
#include <havoqgt/label_propagation_pattern_matching_nonunique_ee.hpp>
//#include <havoqgt/label_propagation_pattern_matching_nonunique_counting_ee.hpp>
//#include <havoqgt/token_passing_pattern_matching.hpp>
//--#include <havoqgt/token_passing_pattern_matching_new.hpp> // TP_ASYNC
//#include <havoqgt/token_passing_pattern_matching_batch.hpp> // TP_BATCH
//#include <havoqgt/token_passing_pattern_matching_iterative.hpp>
//#include <havoqgt/token_passing_pattern_matching_edge_aware.hpp> // TP_ASYNC
//#include <havoqgt/token_passing_pattern_matching_path_checking.hpp> // TP_ASYNC
//#include <havoqgt/token_passing_pattern_matching_nonunique_ee.hpp> // TP_ASYNC // nonunique path checking
//#include <havoqgt/token_passing_pattern_matching_nonunique_nem.hpp> // TP_ASYNC // nonunique path checking, edge monocyclic 
#include <havoqgt/token_passing_pattern_matching_nonunique_nem_1.hpp> // TP_ASYNC // nonunique path checking, edge monocyclic 
//#include <havoqgt/token_passing_pattern_matching_nonunique_tds_1.hpp> // TP_ASYNC // nonunique path checking, edge monocyclic, tds
///#include <havoqgt/token_passing_pattern_matching_nonunique_tds_batch_1.hpp>
#include <havoqgt/token_passing_pattern_matching_nonunique_tds_batch_4.hpp>
//#include <havoqgt/token_passing_pattern_matching_nonunique_iterative_tds_1.hpp> // TP_ASYNC // nonunique path checking, edge monocyclic, tds
//#include <havoqgt/token_passing_pattern_matching_nonunique_iterative_tds_batch_1.hpp> // TP_ASYNC // nonunique path checking, edge monocyclic, tds, batching
//#include <havoqgt/update_edge_state.hpp>
///#include <havoqgt/vertex_data_db.hpp>
#include <havoqgt/vertex_data_db_degree.hpp>*/

//#define OUTPUT_RESULT
//#define ENABLE_BLOCK

#define TP_ASYNC
//#define TP_BATCH

///namespace hmpi = havoqgt::mpi;
///using namespace havoqgt::mpi;
using namespace havoqgt;
//using namespace prunejuice;

typedef havoqgt::distributed_db::segment_manager_type segment_manager_t;

template<typename T>
  using SegmentAllocator = bip::allocator<T, segment_manager_t>;

///typedef hmpi::delegate_partitioned_graph<segment_manager_t> graph_type;
//typedef havoqgt::delegate_partitioned_graph<segment_manager_t> graph_type;
typedef havoqgt::delegate_partitioned_graph
  <typename segment_manager_t::template allocator<void>::type> graph_type;
typedef graph_type DistributedGraph; // DelegateGraph ?

template<typename T>
  using DelegateGraphVertexDataSTDAllocator = graph_type::vertex_data
  <T, std::allocator<T>>; 

template<typename T>
  using DelegateGraphEdgeDataSTDAllocator = graph_type::edge_data
  <T, std::allocator<T>>;  

void usage()  {
  //if(havoqgt_env()->world_comm().rank() == 0) {
  if(comm_world().rank() == 0) {
    std::cerr << "Usage: -i <string> -p <string> -o <string>\n"
      << " -i <string>   - input graph base filename (required)\n"
      << " -b <string>   - backup graph base filename. If set, \"input\" graph will be deleted if it exists\n"
      << " -v <string>   - vertex metadata base filename (optional, default is degree based metadata)\n"
      << " -e <string>   - edge metadata base filename (optional, N/A)\n" 
      << " -p <string>   - pattern base directory (required)\n"
      << " -o <string>   - output base directory (required)\n"
      << " -x <int>      - batch count (optional, default/min batch count is 1, max batch count is "
        << comm_world().size() << "\n"   
      << " -h            - print help and exit\n\n";
  }
}

void parse_cmd_line(int argc, char** argv, std::string& graph_input, 
  std::string& backup_graph_input, std::string& vertex_metadata_input, 
  std::string& edge_metadata_input, std::string& pattern_input, 
  std::string& result_output, uint64_t& tp_vertex_batch_size) {

  //if(havoqgt_env()->world_comm().rank() == 0) {
  if(comm_world().rank() == 0) {
    std::cout << "CMD Line :";
    for (int i=0; i<argc; ++i) {
      std::cout << " " << argv[i];
    }
    std::cout << std::endl;
  }

  bool print_help = false;
  std::bitset<3> required_input;
  required_input.reset();
  required_input.set(2); // TODO: improve

  char c;
  while ((c = getopt(argc, argv, "i:b:v:e:p:o:x:h ")) != -1) {
    switch (c) {
      case 'h' :  
        print_help = true;
        break;  
      case 'i' :
        graph_input = optarg;
        required_input.set(0);
        break;
      case 'b' :
        backup_graph_input = optarg;
        break;			
      case 'v' :
        vertex_metadata_input = optarg;
        break;
      case 'e' :
        edge_metadata_input = optarg;
        break;					
      case 'p' :
        pattern_input = optarg;
        required_input.set(1);   
        break;
      case 'o' :         
        result_output = optarg;
        required_input.set(2);
        break;
      case 'x' :		 	 
        tp_vertex_batch_size = std::stoull(optarg);
        if (tp_vertex_batch_size < 1 || tp_vertex_batch_size > comm_world().size()) {
          print_help = true;
        } else if (tp_vertex_batch_size > 1) {
          tp_vertex_batch_size = comm_world().size() / tp_vertex_batch_size;
        } else {
          tp_vertex_batch_size = comm_world().size();
        } 		
        break;
      default:
        std::cerr << "Unrecognized Option : " << c << ", Ignore."<<std::endl;
        print_help = true;
        break;
    } 		
  }

  if (print_help || !required_input.all()) {
    usage();
    exit(-1);
  }
}

template <typename EditDistancePrototypeVector, typename Uint>
void read_edit_distance_prototype_manifest_file(std::string manifest_filename, 
  EditDistancePrototypeVector& edit_distance_prototype_vector) {
  std::ifstream manifest_file(manifest_filename, std::ifstream::in);
  std::string line;
  while (std::getline(manifest_file, line)) {
    std::istringstream iss(line);
    Uint edit_distance(0), prototype_count(0); 
    iss >> edit_distance >> prototype_count;
    edit_distance_prototype_vector.push_back
      (std::forward_as_tuple(edit_distance, prototype_count));
  } 
  manifest_file.close();
}

int main(int argc, char** argv) {
  //typedef hmpi::delegate_partitioned_graph<segment_manager_t> graph_type;
  //typedef hmpi::delegate_partitioned_graph
  //  <typename segment_manager_t::template allocator<void>::type> graph_type;
  
  int mpi_rank(0), mpi_size(0);

  // havoqgt_init
  //havoqgt::havoqgt_init(&argc, &argv);
  havoqgt::init(&argc, &argv);
  {

  CHK_MPI(MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank));
  CHK_MPI(MPI_Comm_size(MPI_COMM_WORLD, &mpi_size));
  ///havoqgt::get_environment();

  if (mpi_rank == 0) {
    std::cout << "MPI Initialized With " << mpi_size << " Ranks" << std::endl;
    ///havoqgt::get_environment().print();
    //print_system_info(false);
  }
  MPI_Barrier(MPI_COMM_WORLD);  

  /////////////////////////////////////////////////////////////////////////////

  // parse command line
/*  std::string graph_input = argv[1];
  //std::string backup_filename; 

  // for pattern matching
  std::string vertex_data_input_filename = argv[2];
  //std::string pattern_input_filename = argv[3];
  std::string pattern_dir = argv[3];
  std::string vertex_rank_output_filename = argv[4];
  std::string backup_filename = argv[5];
  std::string result_dir = argv[6];
  bool use_degree_as_vertex_data = std::stoull(argv[7]); // 1 - yes, 0 - no 
  bool do_load_graph_from_backup_file = std::stoull(argv[8]); // 1 - yes, 0 - no // TODO: remove
*/
 
  std::string graph_input;
  std::string backup_graph_input;
  std::string vertex_metadata_input;
  std::string edge_metadata_input;
  std::string pattern_input;
  std::string result_output;  
  ///uint64_t tp_vertex_batch_size = havoqgt_env()->world_comm().size();   
  uint64_t tp_vertex_batch_size = comm_world().size();

  parse_cmd_line(argc, argv, graph_input, backup_graph_input, 
    vertex_metadata_input, edge_metadata_input, pattern_input, result_output, 
    tp_vertex_batch_size); 

  std::string pattern_dir = pattern_input; 
  std::string result_dir = "/todo/"; // TODO 

  MPI_Barrier(MPI_COMM_WORLD);

  /////////////////////////////////////////////////////////////////////////////

  // load graph

  if (mpi_rank == 0) {
    std::cout << "Loading Graph ... " << std::endl;
  }

  //if(do_load_graph_from_backup_file) { // TODO: remove do_load_graph_from_backup_file
  //  distributed_db::transfer(backup_filename.c_str(), graph_input.c_str());
  //}
  if (backup_graph_input.size() > 0) {
    distributed_db::transfer(backup_graph_input.c_str(), graph_input.c_str());
  }

  havoqgt::distributed_db ddb(havoqgt::db_open(), graph_input.c_str());

  //segment_manager_t* segment_manager = ddb.get_segment_manager();
  //  bip::allocator<void, segment_manager_t> alloc_inst(segment_manager);

  //graph_type *graph = segment_manager->
  //  find<graph_type>("graph_obj").first;
  //assert(graph != nullptr);  
  graph_type *graph = ddb.get_segment_manager()->
    find<graph_type>("graph_obj").first;
  assert(graph != nullptr);

  // edge data
  //if (mpi_rank == 0) {
    //std::cout << "Loading / Initializing Edge Data ... " << std::endl;
  //}

  typedef uint8_t edge_data_type; 
  // TODO: figure out a way to get it from graph_type
  // see edge_data_value_type in parallel_edge_list_reader.hpp

  typedef graph_type::edge_data<edge_data_type, 
    bip::allocator<edge_data_type, segment_manager_t>> edge_data_t;

  edge_data_t* edge_data_ptr = ddb.get_segment_manager()->
    find<edge_data_t>("graph_edge_data_obj").first;
//  assert(edge_data_ptr != nullptr); 

  MPI_Barrier(MPI_COMM_WORLD);
  if (mpi_rank == 0) {
    std::cout << "Done Loading Graph" << std::endl;
  }

  //graph->print_graph_statistics(); // causes MPI error
  //MPI_Barrier(MPI_COMM_WORLD);  

  /////////////////////////////////////////////////////////////////////////////

  // pattern matching
  {

  // types used by the delegate partitioned graph
  typedef typename graph_type::vertex_iterator vitr_type;
  typedef typename graph_type::vertex_locator vloc_type;
  typedef typename graph_type::edge_iterator eitr_type;
 
  typedef uint64_t Vertex;
  typedef uint64_t Edge;
  typedef uint64_t VertexData; // for string hash
  //typedef uint8_t VertexData; // for log binning 
  //typedef edge_data_type EdgeData;
  typedef uint8_t EdgeData; 
  
  typedef uint64_t VertexRankType; // TODO: delete

  static constexpr size_t max_bit_vector_size = 16; // TODO:
  static constexpr size_t max_template_vertex_count = 16;  
  typedef std::bitset<max_bit_vector_size> BitSet; // TODO: rename to TemplateVertexSet
  typedef BitSet TemplateVertexBitSet; 
  typedef uint16_t TemplateVertexType; // TODO: rename to TemplateVertexBitSetToUint

  static constexpr size_t max_prototype_count = 32;
  typedef std::bitset<max_prototype_count> PrototypeBitSet;
 
  static constexpr size_t max_nonlocal_constraint_count = 1; //16;
  typedef std::bitset<max_nonlocal_constraint_count> NonlocalConstraintBitSet;
 
  typedef uint8_t Boolean; // TODO: replace all bool with Boolean?

  // TODO: mmap
  //typedef graph_type::vertex_data<VertexData, SegmentAllocator<VertexData> > VertexMetadata;
  //typedef graph_type::vertex_data<VertexRankType, SegmentAllocator<VertexRankType> > VertexRank;
  //typedef graph_type::vertex_data<bool, SegmentAllocator<bool> > VertexActive;
  //typedef graph_type::vertex_data<uint64_t, SegmentAllocator<uint64_t> > VertexIteration;

  typedef graph_type::vertex_data<VertexData, std::allocator<VertexData> > VertexMetadata;
  typedef graph_type::vertex_data<Boolean, std::allocator<Boolean> > VertexActive; // TODO: solution_graph // TODO: you are mixing bool and uint!
  typedef graph_type::vertex_data<TemplateVertexType, std::allocator<TemplateVertexType> > TemplateVertex; // TODO: solution_graph, rename to VertexTemplateVertexBitSetToUint
  typedef TemplateVertex VertexTemplateVertexMatches; // TODO: update type name 

  typedef graph_type::vertex_data<uint64_t, std::allocator<uint64_t> > VertexIteration; // TODO: delete
  typedef graph_type::vertex_data<VertexRankType, std::allocator<VertexRankType> > VertexRank; // TODO: delete

  //typedef vertex_state<uint8_t> VertexState;
///  typedef prunejuice::vertex_state_generic<Vertex, VertexData, uint8_t, BitSet> VertexState;
  typedef prunejuice::vertex_state<Vertex, VertexData, BitSet> VertexState;
  typedef std::unordered_map<Vertex, VertexState> VertexStateMap; // TODO: solution_graph

  typedef std::unordered_set<Vertex> VertexSet;  
  typedef graph_type::vertex_data<VertexSet, std::allocator<VertexSet> > VertexSetCollection; 
  
  typedef std::unordered_map<Vertex, uint8_t> VertexUint8Map; 
  typedef graph_type::vertex_data<VertexUint8Map, std::allocator<VertexUint8Map> > VertexUint8MapCollection;  

  typedef std::unordered_map<VertexData, uint64_t> VertexDataUint64Map;
  typedef std::unordered_map<Vertex, VertexDataUint64Map> VertexVertexDataUint64Map;
  typedef graph_type::vertex_data<VertexVertexDataUint64Map, std::allocator<VertexVertexDataUint64Map> > VertexVertexDataUint64MapCollection; 

  typedef std::unordered_map<Vertex, VertexUint8Map> VertexVertexUint8Map; 
  typedef graph_type::vertex_data<VertexVertexUint8Map, std::allocator<VertexVertexUint8Map> > VertexVertexUint8MapCollection; // u--map(p,q)--v 

  //typedef VertexVertexDataUint64MapCollection EdgeStateCollection;
  typedef VertexVertexUint8MapCollection EdgeStateCollection;

  typedef std::unordered_map<Vertex, size_t> VertexSizeMap; 

  typedef graph_type::edge_data<EdgeData, std::allocator<EdgeData> > EdgeMetadata;
  typedef graph_type::edge_data<Boolean, std::allocator<Boolean> > EdgeActive; // TODO: solution_graph 

  typedef std::vector<Boolean> VectorBoolean;

  typedef prunejuice::ApproximateQuery ApproximateQuery;

  typedef graph_type::vertex_data<PrototypeBitSet, 
    std::allocator<PrototypeBitSet> > VertexPrototypeMatches;

  typedef graph_type::vertex_data<NonlocalConstraintBitSet,
    std::allocator<NonlocalConstraintBitSet> > VertexNonlocalConstraintMatches;

  ////////////////////////////////////////////////////////////////////////////// 

  if(mpi_rank == 0) {
    std::cout << "Pattern Matching ... " << std::endl;
  }

  //////////////////////////////////////////////////////////////////////////////

  double time_start = MPI_Wtime();
  double time_end = MPI_Wtime();

  //////////////////////////////////////////////////////////////////////////////

  // per rank containers 

  VertexStateMap vertex_state_map;
  
  VertexSizeMap active_edge_target_vertex_degree_map;

  // vertex containers   
  // TODO: need a new alloc_inst to use bip/mmap
//  VertexMetadata vertex_metadata(*graph, alloc_inst);
//  VertexRank vertex_rank(*graph, alloc_inst);
//  VertexActive vertex_active(*graph, alloc_inst);
//  VertexIteration vertex_iteration(*graph, alloc_inst);
 
  VertexMetadata vertex_metadata(*graph); 
  VertexActive vertex_active(*graph);
  TemplateVertex template_vertices(*graph);
  VertexUint8MapCollection vertex_active_edges_map(*graph);
  VertexSetCollection vertex_token_source_set(*graph); // per vertex set
//--  VertexSetCollection token_source_edge_set(*graph); // per vertex set // edge aware
  EdgeStateCollection vertex_active_edge_state_map(*graph); 

//  VertexRank vertex_rank(*graph);
  uint8_t vertex_rank; // TODO: dummy
//  VertexIteration vertex_iteration(*graph);
  uint8_t vertex_iteration; // TODO: dummy  

  // edge containers
  //EdgeMetadata edge_metadata(*graph);
  ////EdgeActive edge_active(*graph);
  
  // approximate pattern matching 
  //VertexPrototypeMatches vertex_prototype_matches(*graph); 
  VertexSet k_edit_distance_vertex_set(0); // Test 
  // TODO: Important : on each vertex a vector of bitsets, one for each k edit 
  // distance. k + 1 prototypes use the result from k. 
  // Above is a temporary hack. 
    
  VertexNonlocalConstraintMatches vertex_nlc_matches(*graph);

  if(mpi_rank == 0) {
    std::cout << "Pattern Matching | Allocated Vertex and Edge Containers" 
    << std::endl;
  }

  ///////////////////////////////////////////////////////////////////////////// 

  // application parameters // TODO: CLI

  bool is_no_vertex_metadata = true;

  // write vertex data to file
  bool do_output_metadata = false; // TODO: ?

  size_t token_passing_algo = 0; // TODO: ? 

  MPI_Barrier(MPI_COMM_WORLD);
 
  ///////////////////////////////////////////////////////////////////////////// 

  // build the distributed vertex data db
   
  time_start = MPI_Wtime();

  //if (use_degree_as_vertex_data) {
  if (vertex_metadata_input.size() > 0) {
    vertex_data_db_nostdfs<graph_type, VertexMetadata, Vertex, VertexData>
      //(graph, vertex_metadata, vertex_data_input_filename, 10000);      
      (graph, vertex_metadata, vertex_metadata_input, 10000);
      // TODO: each rank reads 10K lines from file at a time
  } else { 
    if (is_no_vertex_metadata) {
      vertex_data_db_same_label<graph_type, VertexMetadata, Vertex, VertexData>
        (graph, vertex_metadata);
    } else {   
      vertex_data_db_degree<graph_type, VertexMetadata, Vertex, VertexData>
        (graph, vertex_metadata);
    }
  }

  MPI_Barrier(MPI_COMM_WORLD); // TODO: do we need this?
  time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | Vertex Data DB : " 
      << time_end - time_start << std::endl;
  }

  // output graph
  if (do_output_metadata) {
    std::string vertex_data_filename = result_dir + //"/" + std::to_string(0) + 
      "/all_ranks_vertex_data/vertex_data_" + std::to_string(mpi_rank);
    std::ofstream vertex_data_file(vertex_data_filename, std::ofstream::out);

    for (vitr_type vitr = graph->vertices_begin(); vitr != graph->vertices_end();
      ++vitr) {
      vloc_type vertex = *vitr;
      vertex_data_file << mpi_rank << ", l, " << graph->locator_to_label(vertex) 
        << ", " << graph->degree(vertex) << ", " << vertex_metadata[vertex] << "\n";  
    } 	
    	
    for(vitr_type vitr = graph->delegate_vertices_begin();
      vitr != graph->delegate_vertices_end(); ++vitr) {
      vloc_type vertex = *vitr;

      if (vertex.is_delegate() && (graph->master(vertex) == mpi_rank)) {
        vertex_data_file << mpi_rank << ", c, " << graph->locator_to_label(vertex) 
          << ", " << graph->degree(vertex) << ", " << vertex_metadata[vertex] << "\n";
      } else {	
        vertex_data_file << mpi_rank << ", d, " << graph->locator_to_label(vertex) 
          << ", " << graph->degree(vertex) << ", " << vertex_metadata[vertex] << "\n";  
      }
    }

    std::string edge_data_filename = result_dir +
      "/all_ranks_edge_data/edge_data_" + std::to_string(mpi_rank);
    std::ofstream edge_data_file(edge_data_filename, std::ofstream::out);
 
    for (vitr_type vitr = graph->vertices_begin(); vitr != graph->vertices_end();
      ++vitr) {
      vloc_type vertex = *vitr;
      for(eitr_type eitr = graph->edges_begin(vertex); 
        eitr != graph->edges_end(vertex); ++eitr) {
        vloc_type neighbor = eitr.target();
        edge_data_file << graph->locator_to_label(vertex) << " " 
          << graph->locator_to_label(neighbor) << "\n";
      }
    }

    for(vitr_type vitr = graph->delegate_vertices_begin();
      vitr != graph->delegate_vertices_end(); ++vitr) {
      vloc_type vertex = *vitr;
      for(eitr_type eitr = graph->edges_begin(vertex);
        eitr != graph->edges_end(vertex); ++eitr) {
        vloc_type neighbor = eitr.target();
        edge_data_file << graph->locator_to_label(vertex) << " "
          << graph->locator_to_label(neighbor) << "\n";
      }
    } 
  
    vertex_data_file.close();
    edge_data_file.close(); 
    
    MPI_Barrier(MPI_COMM_WORLD); // Test
    return 0; // Test
  }
  
  /////////////////////////////////////////////////////////////////////////////

  // read the manifest file for the edit distance based approximate matching

  std::string manifest_filename = pattern_dir + "/manifest"; // TODO: CLI
  typedef std::vector<std::tuple<size_t, size_t>> EditDistancePrototypeVector;
  EditDistancePrototypeVector edit_distance_prototype_vector;

  read_edit_distance_prototype_manifest_file
    <EditDistancePrototypeVector, size_t>
    (manifest_filename, edit_distance_prototype_vector);  
  
  // Test
  if (mpi_rank == 0) {
    std::cout << "Edit Distance, Prototype Count" << std::endl; 
    for (size_t i = 0; i < edit_distance_prototype_vector.size(); i++) {
      std::cout << std::get<0>(edit_distance_prototype_vector[i]) << " "
        << std::get<1>(edit_distance_prototype_vector[i]) << std::endl;  
    }
  } 
  // Test
  
  MPI_Barrier(MPI_COMM_WORLD);  

  //return 0; // Test

  ///////////////////////////////////////////////////////////////////////////// 
 
  // result
   
  std::string pattern_set_result_filename = result_dir + "/result_pattern_set";
  std::ofstream pattern_set_result_file;  
  if (mpi_rank == 0) {
    pattern_set_result_file = std::ofstream(pattern_set_result_filename, 
      std::ofstream::out);
  }

  /////////////////////////////////////////////////////////////////////////////

  double application_time_start = MPI_Wtime();
  double application_time_end = MPI_Wtime();

  bool do_nonlocal_constraint_checking = false; // TODO: Boolean

  bool do_generate_max_candidate_set = false; // TODO: Boolean

  bool do_subgraph_indexing = false; // TODO: Boolean

  size_t all_edit_distance_vertex_set_size_global = 0;
 
  // loop over up to k-edit distance prototypes
  
  for (size_t pk_tmp = 0; pk_tmp <= edit_distance_prototype_vector.size(); 
    pk_tmp++) {

    size_t pk = 0;

    if (pk_tmp > 0) {
      pk = pk_tmp - 1; 
      do_generate_max_candidate_set = false;
      continue; // TODO:           
    } else { 
      //do_generate_max_candidate_set = true;
      //continue; // TODO:	
      do_subgraph_indexing = true;
    }  

    double edit_distance_time_start = MPI_Wtime();
    double edit_distance_time_end = MPI_Wtime();

    size_t current_edit_distance = 
      std::get<0>(edit_distance_prototype_vector[pk]);
    if (mpi_rank == 0) { // TODO: 
      if (do_generate_max_candidate_set) {
        std::cout << "Pattern Matching | Generating Max Candidate Set ... " 
        << std::endl; 
      } else {  
        std::cout << "Pattern Matching | Searching Edit Distance " 
          << current_edit_distance << " Prototypes ... " << std::endl;  
      }
    }

    //continue; // Test
 
  /////////////////////////////////////////////////////////////////////////////

  // TODO: setup pattern set 
  // a pattern set is a collection of directories containing pattern files 
  
  // TODO: code indentation

  // loop over pattern set
  
  //size_t max_ps = 1; // Test
  size_t max_ps = std::get<1>(edit_distance_prototype_vector[pk]); // Test // prototype_count 

  //size_t max_ps = 7 + 2; // RMAT // Test
  //size_t max_ps = 15 + 2; // RMAT //11 + 1; // Test
  //size_t max_ps = 5 + 2; // RDT_25_4 // Test
  
  //size_t max_ps = 6 + 2; // WDC_patterns_16_2
  //size_t max_ps = 15 + 2; // WDC_patterns_16_2

  //size_t max_ps = 1 + 7 + 12; // WDC_patterns_12_C 

  //size_t max_ps = 8 + 2; // wdc_patterns_31_c/
  //size_t max_ps = 32 + 2; // wdc_patterns_31_c/
  //size_t max_ps = 60 + 2; // wdc_patterns_31_c/
  //size_t max_ps = 47 + 2; // wdc_patterns_31_c/   
  
  //size_t max_ps = 1 + 9 + 33 + 61 + 48; // wdc_patterns_31_c/
 
  //size_t max_ps = 1 + 6 + 15 + 16; // ER, ML, 4U
  //size_t max_ps = 1 + 2 + 4 + 4; // ER, ML, 1U
  //size_t max_ps = 1 + 4 + 9 + 9; // ER, ML, 2U

  //std::string pattern_root_dir = pattern_dir;
  //std::string pattern_output_dir = ""; 

  // loop over k-edit distance prototypes 
   
  for (size_t ps = 0; ps < max_ps; ps++) { // TODO: for now, only reading from pattern_dir/0 ? 
  //for (size_t ps = 0; ps < 1; ps++) {

  double prototype_time_start = MPI_Wtime();
  double prototype_time_end = MPI_Wtime();

  // beginning of the pattern set
   
  // setup pattern to search
  if(mpi_rank == 0) { 
    //std::cout << "Setting up Pattern [" << ps << "] ... " << std::endl;
    std::cout << "Pattern Matching | Setting up Prototype [" 
      << current_edit_distance << "]["
      << ps << "] ... " << std::endl;   
  }
   
  // setup pattern - for local constraint checking 

  //////////////////////////////////////////////////////////////////////////////

  // Test
  
  // ER, ML

  // 4U 
/*  if (ps == 0) { 
    pattern_dir = pattern_root_dir + "/0_" + std::to_string(ps) + "/pattern";
    pattern_output_dir = pattern_root_dir + "/0_" + std::to_string(ps) + "/output/";
  } else if (ps >= 1 && ps <= 6) {
    pattern_dir = pattern_root_dir + "/1_" + std::to_string(ps - 1) + "/pattern";
    pattern_output_dir = pattern_root_dir + "/1_" + std::to_string(ps - 1) + "/output/";
    //continue;
  } else if (ps >= 7 && ps <= 21) {
    pattern_dir = pattern_root_dir + "/2_" + std::to_string(ps - 7) + "/pattern";
    pattern_output_dir = pattern_root_dir + "/2_" + std::to_string(ps - 7) + "/output/"; 
    //continue;
  } else if (ps >= 22 && ps <= 37) {
    pattern_dir = pattern_root_dir + "/3_" + std::to_string(ps - 22) + "/pattern";
    pattern_output_dir = pattern_root_dir + "/3_" + std::to_string(ps - 22) + "/output/";
    //continue;
  } else {
    std::cerr << "Error: wrong value." << std::endl;
    continue;
  }*/  

  // 1U
  /*if (ps == 0) { 
    pattern_dir = pattern_root_dir + "/0_" + std::to_string(ps) + "/pattern";
    pattern_output_dir = pattern_root_dir + "/0_" + std::to_string(ps) + "/output/";
  } else if (ps >= 1 && ps <= 2) {
    pattern_dir = pattern_root_dir + "/1_" + std::to_string(ps - 1) + "/pattern";
    pattern_output_dir = pattern_root_dir + "/1_" + std::to_string(ps - 1) + "/output/";
    //continue;
  } else if (ps >= 3 && ps <= 6) {
    pattern_dir = pattern_root_dir + "/2_" + std::to_string(ps - 3) + "/pattern";
    pattern_output_dir = pattern_root_dir + "/2_" + std::to_string(ps - 3) + "/output/"; 
    //continue;
  } else if (ps >= 7 && ps <= 10) {
    pattern_dir = pattern_root_dir + "/3_" + std::to_string(ps - 7) + "/pattern";
    pattern_output_dir = pattern_root_dir + "/3_" + std::to_string(ps - 7) + "/output/";
    //continue;
  } else {
    std::cerr << "Error: wrong value." << std::endl;
    continue;
  }*/

  // 2U
  /*if (ps == 0) { 
    pattern_dir = pattern_root_dir + "/0_" + std::to_string(ps) + "/pattern";
    pattern_output_dir = pattern_root_dir + "/0_" + std::to_string(ps) + "/output/";
  } else if (ps >= 1 && ps <= 4) {
    pattern_dir = pattern_root_dir + "/1_" + std::to_string(ps - 1) + "/pattern";
    pattern_output_dir = pattern_root_dir + "/1_" + std::to_string(ps - 1) + "/output/";
    //continue;
  } else if (ps >= 5 && ps <= 13) {
    pattern_dir = pattern_root_dir + "/2_" + std::to_string(ps - 5) + "/pattern";
    pattern_output_dir = pattern_root_dir + "/2_" + std::to_string(ps - 5) + "/output/"; 
    //continue;
  } else if (ps >= 14 && ps <= 22) {
    pattern_dir = pattern_root_dir + "/3_" + std::to_string(ps - 14) + "/pattern";
    pattern_output_dir = pattern_root_dir + "/3_" + std::to_string(ps - 14) + "/output/";
    //continue;
  } else {
    std::cerr << "Error: wrong value." << std::endl;
    continue;
  }*/ 
 
  // Test

  // WDC_12_C
  // quartz 
  /*if (ps == 0) { 
    pattern_dir = "/p/lscratchh/reza2/tmp_1/WDC_patterns_12_C/tmp_1/0_" + std::to_string(ps) + "/pattern";
  } else {
    pattern_dir = "/p/lscratchh/reza2/tmp_1/WDC_patterns_12_C/tmp_1/1_" + std::to_string(ps - 1) + "/pattern";   
  }

  // not running 0_0 / k=0 / not checkign common non-local constraints / k's do not have any   
  //pattern_dir = "/p/lscratchh/reza2/tmp_1/WDC_patterns_12_C/tmp_2/2_" + std::to_string(ps) + "/pattern";*/

  /*if (ps == 0) {
    pattern_dir = "/p/lscratchh/reza2/tmp_1/WDC_patterns_12_C/tmp_2/0_" + std::to_string(ps) + "/pattern";
  } else if (ps >= 1 && ps <= 7) { 
    pattern_dir = "/p/lscratchh/reza2/tmp_1/WDC_patterns_12_C/tmp_2/1_" + std::to_string(ps - 1) + "/pattern";
  } else if (ps >= 8 && ps <= 19) {
    pattern_dir = "/p/lscratchh/reza2/tmp_1/WDC_patterns_12_C/tmp_2/2_" + std::to_string(ps - 8) + "/pattern";
  } else {
    std::cerr << "Error: wrong value." << std::endl;
    continue; 
  }*/ 

  // catalyst
  //  pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/WDC_patterns_16_2/tmp_1/";

  // WDC_16_2
  // quartz
  // TODO:
  /*if (ps == 0) {
    //continue; 
    pattern_dir = "/p/lustre2/reza2/tmp_1/WDC_patterns_16_2/tmp_1/0_" + std::to_string(ps) + "/pattern";
  } else {
    //if ((ps - 1) != 31) {
    //  continue;
    //}
    pattern_dir = "/p/lustre2/reza2/tmp_1/WDC_patterns_16_2/tmp_1/2_" + std::to_string(ps - 1) + "/pattern";   
  }*/

  // WDC_patterns_31_C/
  // catalyst 
  /*if (ps == 0) {
    //continue; 
    pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/WDC_patterns_31_C/tmp_1/0_" + std::to_string(ps) + "/pattern";
  } else {
    //if ((ps - 1) != 31) {
    //  continue;
    //}
    pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/WDC_patterns_31_C/tmp_1/1_" + std::to_string(ps - 1) + "/pattern";   
  }*/

  // skip token 
  /*if (ps == 0) {
    pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/WDC_patterns_31_C/tmp_4/0_" + std::to_string(ps) + "/pattern";
  } else if (ps >= 1 && ps <= 9) {
    pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/WDC_patterns_31_C/tmp_4/1_" + std::to_string(ps - 1) + "/pattern";
  } else if (ps >= 10 && ps <= 42) {
    pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/WDC_patterns_31_C/tmp_4/2_" + std::to_string(ps - 10) + "/pattern";
  } else if (ps >= 43 && ps <= 103) {
    pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/WDC_patterns_31_C/tmp_4/3_" + std::to_string(ps - 43) + "/pattern"; 
  } else {
    std::cerr << "Error: wrong value." << std::endl;
    continue;
  }*/ 

  /*if (ps == 0) {
    pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/WDC_patterns_31_C/tmp_4/0_" + std::to_string(ps) + "/pattern";
  } else if (ps >= 95 && ps <= 103) {
    pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/WDC_patterns_31_C/tmp_4/1_" + std::to_string(ps - 95) + "/pattern";
  } else if (ps >= 62 && ps <= 94) {
    pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/WDC_patterns_31_C/tmp_4/2_" + std::to_string(ps - 62) + "/pattern";
  } else if (ps >= 1 && ps <= 61) {
    pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/WDC_patterns_31_C/tmp_4/3_" + std::to_string(ps - 1) + "/pattern"; 
  } else {
    std::cerr << "Error: wrong value." << std::endl;
    continue;
  }*/ 

  // quartz
  // TODO:
  /*if (ps == 0) {
    //continue; 
    pattern_dir = "/p/lustre2/reza2/tmp_1/WDC_patterns_31_C/tmp_1/0_" + std::to_string(ps) + "/pattern";
  } else {
    //if ((ps - 1) != 31) {
    //  continue;
    //}
    pattern_dir = "/p/lustre2/reza2/tmp_1/WDC_patterns_31_C/tmp_1/3_" + std::to_string(ps - 1) + "/pattern";   
  }*/

  /*if (ps == 0) {
    //continue;
    pattern_dir = "/p/lustre2/reza2/tmp_1/WDC_patterns_31_C/tmp_4/0_" + std::to_string(ps) + "/pattern";
  } else if (ps >= 95 && ps <= 103) {
    continue;
    pattern_dir = "/p/lustre2/reza2/tmp_1/WDC_patterns_31_C/tmp_4/1_" + std::to_string(ps - 95) + "/pattern";
  } else if (ps >= 62 && ps <= 94) {
    continue;
    pattern_dir = "/p/lustre2/reza2/tmp_1/WDC_patterns_31_C/tmp_4/2_" + std::to_string(ps - 62) + "/pattern";
  } else if (ps >= 1 && ps <= 61) {
    continue;  
    pattern_dir = "/p/lustre2/reza2/tmp_1/WDC_patterns_31_C/tmp_4/3_" + std::to_string(ps - 1) + "/pattern"; 
  } else if (ps >= 104 && ps <= 151) {
    //continue;  
    pattern_dir = "/p/lustre2/reza2/tmp_1/WDC_patterns_31_C/tmp_4/4_" + std::to_string(ps - 104) + "/pattern";
  }   else {
    std::cerr << "Error: wrong value." << std::endl;
    continue;
  }*/ 

  // RMAT
  /*if (ps == 0) {
    //continue;
    pattern_dir = "/p/lscratchh/reza2/tmp_1/RMAT_4/tmp_2/0_" + std::to_string(ps) + "/pattern";
  } else {
    //continue; 
    pattern_dir = "/p/lscratchh/reza2/tmp_1/RMAT_4/tmp_2/2_" + std::to_string(ps - 1) + "/pattern";
  }*/

  // enumeration
  /*if (ps == 0) {
    //continue;
    pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/RMAT_4/tmp_3/0_" + std::to_string(ps) + "/pattern";
  } else {
    //continue; 
    pattern_dir = "/p/lustre3/havoqgtu/reza2_tmp/apm_1/RMAT_4/tmp_3/2_" + std::to_string(ps - 1) + "/pattern";
  }*/

  //pattern_dir = "/p/lscratchh/reza2/tmp_1/RMAT_4/tmp_1/2_" + std::to_string(ps) + "/pattern";
  
  // RDT_25_4
  /*if (ps == 0) {
    continue; 
    pattern_dir = "/p/lscratchh/reza2/tmp_1/RDT_25_4/tmp_1/0_" + std::to_string(ps) + "/pattern";
  } else {
    pattern_dir = "/p/lscratchh/reza2/tmp_1/RDT_25_4/tmp_1/1_" + std::to_string(ps - 1) + "/pattern"; 
  }*/

  // enumeration
  /*if (ps == 0) {
    continue; 
    pattern_dir = "/p/lscratchh/reza2/tmp_1/RDT_25_4/tmp_2/0_" + std::to_string(ps) + "/pattern";
  } else {
    pattern_dir = "/p/lscratchh/reza2/tmp_1/RDT_25_4/tmp_2/1_" + std::to_string(ps - 1) + "/pattern"; 
  }*/
   
  //pattern_dir = "/p/lscratchh/reza2/tmp_1/RDT_25_4/tmp_1/1_" + std::to_string(ps) + "/pattern";
  
  //////////////////////////////////////////////////////////////////////////////

  // prototype input/output directory
  std::string pattern_input_dir = pattern_dir + "/" + 
    std::to_string(current_edit_distance) + "_" + std::to_string(ps) + "/pattern";
  std::string pattern_output_dir = pattern_dir + "/" +
    std::to_string(current_edit_distance) + "_" + std::to_string(ps) + "/output"; 

  if(mpi_rank == 0) {
    //std::cout << "Pattern Matching | Reading Pattern From " << pattern_dir << std::endl; 
    //std::cout << "Pattern Matching | Output Directory " << pattern_output_dir << std::endl; 
    std::cout << "Pattern Matching | Reading Input From : " << pattern_input_dir << std::endl; 
    std::cout << "Pattern Matching | Output Directory : " << pattern_output_dir << std::endl; 
  }

  //continue; // Test
 
  // Test
 
  //std::string pattern_input_filename = pattern_dir + "/" + std::to_string(ps) + "/pattern";
  std::string pattern_input_filename = pattern_input_dir + "/pattern";
  
  typedef prunejuice::pattern_graph_csr<Vertex, Edge, VertexData,
    EdgeData, TemplateVertexBitSet, Boolean, ApproximateQuery> PatternGraph;

  PatternGraph pattern_graph(
    pattern_input_filename + "_edge",
    pattern_input_filename + "_vertex",
    pattern_input_filename + "_vertex_data",
    pattern_input_filename + "_edge_data",
    pattern_input_filename + "_stat",
    false, false); // TODO: improve

  // TODO: create a graphical representation of the pattern 
  // using a 2D graphics library
  // pattern information
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching | Searching Pattern [" << pk << "][" << ps
      << "] : " << std::endl;
    for (auto v = 0; v < pattern_graph.vertex_count; v++) {
      std::cout << v << " : offset " << pattern_graph.vertices[v] 
      << " vertex_data " << pattern_graph.vertex_data[v] 
      << " vertex_degree " << pattern_graph.vertex_degree[v] << std::endl;
      std::cout << " neighbors : "; 
      for (auto e = pattern_graph.vertices[v]; e < pattern_graph.vertices[v + 1]; e++) {
        auto v_nbr = pattern_graph.edges[e];
        std::cout << v_nbr << ", " ;
      }
      std::cout << std::endl;
      std::cout << " neighbor vertex data count : ";
      /*16MAR2019 for (auto& nd : pattern_graph.vertex_neighbor_metadata_count_map[v]) {
        std::cout << "(" << nd.first << ", " << nd.second << ") ";
      }*/
      std::cout << std::endl; 
    }
    //std::cout << "diameter : TODO" << std::endl; //<< pattern_graph.diameter << std::endl; // TODO:
  }  

  // TODO: remove from here    
  // setup pattern - for token passing
   
  //pattern_util<VertexData, Vertex> ptrn_util_two(pattern_input_filename, true); 
  //pattern_util<VertexData, Vertex> ptrn_util_two(pattern_input_filename + "_nem", true);
  
  //typedef pattern_util<VertexData, Vertex> PatternUtilities;
  
  //PatternUtilities ptrn_util_two(pattern_input_filename + "_nlc", 
  //  pattern_input_filename + "_non_local_constraint", true, true);

  //PatternUtilities ptrn_util_two(pattern_input_filename + "_nem", pattern_input_filename + "_tds", true, true);
  //pattern_util<VertexData, Vertex> ptrn_util_two(pattern_input_filename + "_pc", true);

  //PatternUtilities ptrn_util_two(pattern_input_filename + "_nlc",
  // pattern_input_filename + "_non_local_constraint", 
  // pattern_input_filename + "_aggregation", true, true);
  
  //MPI_Barrier(MPI_COMM_WORLD); // TODO: ?
  
  typedef pattern_nonlocal_constraint<Vertex, Edge, VertexData, PatternGraph>
    PatternNonlocalConstraint;

  //PatternNonlocalConstraint ptrn_util_two(pattern_graph,
  //  pattern_input_filename + "_non_local_constraints",
  //  pattern_input_filename + "vertex_non_local_constraints");     

  PatternNonlocalConstraint ptrn_util_two(pattern_graph,
    //pattern_input_filename + "_nonlocal_constraint");
    //pattern_dir + "/pattern_nonlocal_constraint"); 
    pattern_input_dir + "/pattern_nonlocal_constraint");

  //auto pattern = std::get<0>(ptrn_util_two.input_patterns[0]); // TODO: remove
  //auto pattern_indices = std::get<1>(ptrn_util_two.input_patterns[0]); // TODO: remove

  // Test
  //if(mpi_rank == 0) {
  //  std::cout << "Token Passing | Agreegation Steps : " << std::endl;
  //  for (auto i : ptrn_util_two.aggregation_steps) {  
  //    pattern_util<uint8_t>::output_pattern(i);
  //  }
  //} 
  // Test

  //MPI_Barrier(MPI_COMM_WORLD); // Test
  //return 0; // Test
  //continue; // Test  

  //////////////////////////////////////////////////////////////////////////////

  // per pattern initialization

  // initialize containers
  vertex_state_map.clear(); // important
  //vertex_rank.reset(0);
  active_edge_target_vertex_degree_map.clear();
  
  vertex_active.reset(true); // initially all vertices are active
  template_vertices.reset(0);

  //vertex_iteration.reset(0); // TODO: -1 ?
  vertex_active_edges_map.clear(); // important
  vertex_token_source_set.clear(); // clear all the sets on all the vertices
  //edge_metadata.reset(55); // Test
  //edge_active.reset(0); //  initially all edges are active / inactive

  vertex_active_edge_state_map.clear();

  // initialize application parameters  
  bool global_init_step = true; // TODO: Boolean 
  bool global_not_finished = false; // TODO: Boolean

  //bool do_nonlocal_constraint_checking = true; // TODO: Boolean
  do_nonlocal_constraint_checking = true; // TODO: Boolean

  uint64_t global_itr_count = 0;

  uint64_t active_vertices_count = 0;
  uint64_t active_edges_count = 0;

  uint64_t message_count = 0; 

  //////////////////////////////////////////////////////////////////////////////

  // result
   
  std::string itr_result_filename = result_dir + //"/" + std::to_string(ps) + 
    "/result_iteration"; // TODO: improve
  std::ofstream itr_result_file(itr_result_filename, std::ofstream::out);

  std::string step_result_filename = result_dir + //"/" + std::to_string(ps) 
    "/result_step";
  std::ofstream step_result_file(step_result_filename, std::ofstream::out); 

  std::string superstep_result_filename = result_dir + //"/" + std::to_string(ps) + 
    "/result_superstep";
  std::ofstream superstep_result_file(superstep_result_filename, std::ofstream::out);

  std::string active_vertices_count_result_filename = result_dir + //"/" + std::to_string(ps) + 
    "/all_ranks_active_vertices_count/active_vertices_" + std::to_string(mpi_rank); 
  std::ofstream active_vertices_count_result_file(active_vertices_count_result_filename, std::ofstream::out);

  // TODO: revert changes 

  std::string active_vertices_result_filename = result_dir + //"/" + std::to_string(ps) +
  //std::string active_vertices_result_filename = pattern_output_dir +   
    "/all_ranks_active_vertices/active_vertices_" + std::to_string(mpi_rank);
  std::ofstream active_vertices_result_file(active_vertices_result_filename, std::ofstream::out);

  std::string active_edges_count_result_filename = result_dir + //"/" + std::to_string(ps) + 
    "/all_ranks_active_edges_count/active_edges_" + std::to_string(mpi_rank);
  std::ofstream active_edges_count_result_file(active_edges_count_result_filename, std::ofstream::out);

  std::string active_edges_result_filename = result_dir + //"/" + std::to_string(ps) + 
    "/all_ranks_active_edges/active_edges_" + std::to_string(mpi_rank);
  std::ofstream active_edges_result_file(active_edges_result_filename, std::ofstream::out);

  //std::string paths_result_filename = result_dir + "/" +
  //  std::to_string(ps) + "/all_ranks_paths/paths_" + std::to_string(mpi_rank);
  //std::ofstream paths_result_file(paths_result_filename, std::ofstream::out);

  std::string message_count_result_filename = result_dir + //"/" + std::to_string(ps) + 
    "/all_ranks_messages/messages_" + std::to_string(mpi_rank); // TODO: message_count
  std::ofstream message_count_result_file(message_count_result_filename, std::ofstream::out);

  std::string pattern_neighbors_result_filename = result_dir + //"/" + std::to_string(ps) +
  //std::string pattern_neighbors_result_filename = pattern_output_dir +
    "/all_ranks_pattern_neighbors/neighbors_" + std::to_string(mpi_rank);
  std::ofstream pattern_neighbors_result_file(pattern_neighbors_result_filename, std::ofstream::out);

  MPI_Barrier(MPI_COMM_WORLD);

  /////////////////////////////////////////////////////////////////////////////

  // run application
///  do {
  
  if (mpi_rank == 0) {
    std::cout << "Pattern Matching | Running Constraint Checking ..." << std::endl;
  }

  //global_not_finished = false;

  double pattern_time_start = MPI_Wtime();

  double itr_time_start = MPI_Wtime();

  //continue; // Test 

  /////////////////////////////////////////////////////////////////////////////

  // identify and delete invalid edges
  //update_edge_state();

  /////////////////////////////////////////////////////////////////////////////
  
//#ifdef ENABLE_BLOCK

  // label propagation  
    
  double label_propagation_time_start = MPI_Wtime();
  double label_propagation_time_end = MPI_Wtime();

  // clone pattern matchng
  //fuzzy_pattern_matching(graph, vertex_metadata, pattern, pattern_indices, vertex_rank);

  // label propagation pattern matching 
//  label_propagation_pattern_matching<graph_type, VertexMetaData, VertexData, decltype(pattern), decltype(pattern_indices), 
//    VertexRank, VertexActive, VertexIteration, VertexStateMap, PatternGraph>
//    (graph, vertex_metadata, pattern, pattern_indices, vertex_rank, vertex_active, 
//    vertex_iteration, vertex_state_map, pattern_graph);

  // label propagation pattern matching bsp, iterative 
//  label_propagation_pattern_matching_bsp<graph_type, VertexMetaData, VertexData, decltype(pattern), decltype(pattern_indices), 
//    /*VertexRank*/uint8_t, VertexActive, /*VertexIteration*/uint8_t, VertexStateMap, PatternGraph, EdgeActive, VertexSetCollection,
//    EdgeMetadata>
//    (graph, vertex_metadata, pattern, pattern_indices, vertex_rank, vertex_active, 
//    vertex_iteration, vertex_state_map, pattern_graph, global_init_step, global_not_finished, 
//    global_itr_count, superstep_result_file, active_vertices_count_result_file, edge_active/**edge_data_ptr*/, edge_metadata);

 if (do_generate_max_candidate_set) {
   prunejuice::approximate::local_constraint_checking_any_neighbor<Vertex,
     VertexData, graph_type, VertexMetadata, VertexStateMap, VertexActive,
     VertexUint8MapCollection, TemplateVertexBitSet, TemplateVertex, PatternGraph>
     (graph, vertex_metadata, vertex_state_map, vertex_active,
     vertex_active_edges_map, template_vertices, pattern_graph, global_init_step,
     global_not_finished, global_itr_count, superstep_result_file,
     active_vertices_count_result_file, active_edges_count_result_file,
     message_count_result_file);

   MPI_Barrier(MPI_COMM_WORLD);
   label_propagation_time_end = MPI_Wtime();
   if(mpi_rank == 0) {
     std::cout << "Pattern Matching Time | Max Candidate Set Generation : "
       << label_propagation_time_end - label_propagation_time_start << std::endl;
   }
 
   // result 
   if(mpi_rank == 0) {
     step_result_file << global_itr_count << ", MC, " << "0"  << ", "
       << (label_propagation_time_end - label_propagation_time_start) << "\n";
   }

   do_nonlocal_constraint_checking = false; // Test 
   
   //continue; // Test 	     

 } else if (do_subgraph_indexing) { 
   prunejuice::local_constraint_checking_3clique<Vertex, VertexData,
     graph_type, VertexMetadata, VertexStateMap, VertexActive,
     VertexUint8MapCollection, TemplateVertexBitSet, TemplateVertex, PatternGraph, 
     VertexSizeMap>
     (graph, vertex_metadata, vertex_state_map, vertex_active,
     vertex_active_edges_map, template_vertices, pattern_graph, 
     active_edge_target_vertex_degree_map, global_init_step,
     global_not_finished, global_itr_count, superstep_result_file,
     active_vertices_count_result_file, active_edges_count_result_file,
     message_count_result_file); 

   MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
   label_propagation_time_end = MPI_Wtime();
   if(mpi_rank == 0) {
     std::cout << "Pattern Matching Time | Local Constraint Checking (Subgraph Indexing) : "
       << label_propagation_time_end - label_propagation_time_start << std::endl;
   }

   // result
   if(mpi_rank == 0) {
     step_result_file << global_itr_count << ", LP, " << "0"  << ", "
       << (label_propagation_time_end - label_propagation_time_start) << "\n";
   }

   do_nonlocal_constraint_checking = false; // Test

   if (global_init_step) { // Important
    global_init_step = false;
   }
 
   // global termination detection 
   global_not_finished = havoqgt::mpi_all_reduce(global_not_finished, std::greater<uint8_t>(), MPI_COMM_WORLD);
   MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here 

   if(mpi_rank == 0) {
     std::cout << "Pattern Matching | Global Finished Status : "; 
     if (global_not_finished) { 
       std::cout << "Continue" << std::endl;
     } else {
       std::cout << "Stop" << std::endl;
       do_nonlocal_constraint_checking = false; 
     } 
   }

   // Test
   // global active vertex count // TODO: do this with local constraint checking ? 
   size_t global_active_vertices_count =  havoqgt::mpi_all_reduce(vertex_state_map.size(),
     std::greater<size_t>(), MPI_COMM_WORLD);
   MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
   if (global_active_vertices_count < 1) {
     //break;
     global_not_finished = false;
     do_nonlocal_constraint_checking = false; 
   } 
   // Test

   // Test
   if(mpi_rank == 0) {
     std::cout << " - | Edge Target Vertex Count : " << active_edge_target_vertex_degree_map.size() << std::endl;  
   }
   // Test

   //continue; // Test
   
 } else {

 prunejuice::label_propagation_pattern_matching_bsp<Vertex, VertexData, 
   graph_type, VertexMetadata, VertexStateMap, VertexActive, 
   VertexUint8MapCollection, TemplateVertexBitSet, TemplateVertex, PatternGraph>
   (graph, vertex_metadata, vertex_state_map, vertex_active, 
   vertex_active_edges_map, template_vertices, pattern_graph, global_init_step, 
   global_not_finished, global_itr_count, superstep_result_file, 
   active_vertices_count_result_file, active_edges_count_result_file,
   message_count_result_file);

  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
  label_propagation_time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | Local Constraint Checking : " 
      << label_propagation_time_end - label_propagation_time_start << std::endl;
  }

  // result
  if(mpi_rank == 0) {
    step_result_file << global_itr_count << ", LP, " << "0"  << ", "
      << (label_propagation_time_end - label_propagation_time_start) << "\n";
  }

//#endif

  /////////////////////////////////////////////////////////////////////////////

  if (global_init_step) { // Important
    global_init_step = false;
  } 

  // global termination detection 
  //std::cout << "Fuzzy Pattern Matching | Global Not Finished status (local) : " << global_not_finished << std::endl; // Test
  // global_not_finished = havoqgt::mpi::mpi_all_reduce(global_not_finished, std::logical_or<bool>(), MPI_COMM_WORLD); // does not work
  ///global_not_finished = havoqgt::mpi::mpi_all_reduce(global_not_finished, std::greater<uint8_t>(), MPI_COMM_WORLD); 
  global_not_finished = havoqgt::mpi_all_reduce(global_not_finished, std::greater<uint8_t>(), MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here 

  if(mpi_rank == 0) {
    std::cout << "Pattern Matching | Global Finished Status : "; 
    if (global_not_finished) { 
      std::cout << "Continue" << std::endl;
    } else {
      std::cout << "Stop" << std::endl;
      do_nonlocal_constraint_checking = false; 
    } 
  }

  // global verification - are all vertex_state_maps empty
  // false - no active vertex left, true - active vertices left 
  //bool global_active_vertex = true; //vertex_state_map.size() < 1 ? false : true;
  //if (vertex_state_map.size() < 1) {
//    global_active_vertex = false;
  //}

  // Test
  // global active vertex count // TODO: do this with local constraint checking ? 
  size_t global_active_vertices_count =  havoqgt::mpi_all_reduce(vertex_state_map.size(),
    std::greater<size_t>(), MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
  if (global_active_vertices_count < 1) {
    //break;
    global_not_finished = false;
    do_nonlocal_constraint_checking = false; 
  } 
  // Test

  //continue; // Test 
  
  } // else - do_generate_max_candidate_set

  //////////////////////////////////////////////////////////////////////////////

  // TODO: What is the issue here? Preventing stdout from this file beyond this point.  	
  // global_active_vertex = havoqgt::mpi::mpi_all_reduce(global_active_vertex, std::greater<uint8_t>(), MPI_COMM_WORLD); 
  // TODO: not working properly - why? // bool does not work
  //MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here

//  global_not_finished = global_active_vertex; // TODO: verify and fix

  /*if(mpi_rank == 0) {
    std::cout << "Pattern Matching | Global Active Vertex Status : ";
    if (global_active_vertex) {
      std::cout << "Active vertices left." << std::endl; // TODO: ignore for now
    } else {
      std::cout << "No active vertex left." << std::endl;
    }
  }*/

  // Test
/*   uint64_t vertex_active_count = 0;
   uint64_t vertex_inactive_count = 0;
   for (vitr_type vitr = graph->vertices_begin(); vitr != graph->vertices_end();
      ++vitr) {
      vloc_type vertex = *vitr;
      if (vertex_active[vertex]) {
        //std::cout << mpi_rank << ", l, " << graph->locator_to_label(vertex) 
        //  << ", " << graph->degree(vertex) << ", " << vertex_metadata[vertex] << "\n";  
        //  vertex_active_count++;
      } else { 
        vertex_inactive_count++;
      }  
    } 	
    	
    for(vitr_type vitr = graph->delegate_vertices_begin();
      vitr != graph->delegate_vertices_end(); ++vitr) {
      vloc_type vertex = *vitr;
      if (vertex_active[vertex]) {  
        if (vertex.is_delegate() && (graph->master(vertex) == mpi_rank)) {
          //std::cout << mpi_rank << ", c, " << graph->locator_to_label(vertex) 
          //  << ", " << graph->degree(vertex) << ", " << vertex_metadata[vertex] << "\n";
        } else {	
          //std::cout << mpi_rank << ", d, " << graph->locator_to_label(vertex) 
          //  << ", " << graph->degree(vertex) << ", " << vertex_metadata[vertex] << "\n"; 
        }
        vertex_active_count++; 
      } else {
        vertex_inactive_count++;
      } 
    }

    //std::cout << mpi_rank << " vertex_active_count " << vertex_active_count << std::endl; 
    //std::cout << mpi_rank << " vertex_inactive_count " << vertex_inactive_count << std::endl;
    //std::cout << mpi_rank << " vertex_state_map size " << vertex_state_map.size() << std::endl;      
*/
  // Test

  /////////////////////////////////////////////////////////////////////////////

  // three clique edge state

  time_start = MPI_Wtime();

  prunejuice::indexing::three_clique_edge_state_batch<graph_type, Vertex, Edge, VertexData,
    EdgeData, VertexMetadata, EdgeMetadata, VertexActive,
    VertexUint8MapCollection, TemplateVertex, VertexStateMap, PatternGraph,
    /*PatternUtilities*/PatternNonlocalConstraint, VertexUint8Map, VertexSetCollection,
    DelegateGraphVertexDataSTDAllocator, Boolean, BitSet, VertexNonlocalConstraintMatches,
    EdgeStateCollection, VertexSizeMap>
    (graph, vertex_metadata, vertex_active, vertex_active_edges_map,
    template_vertices, vertex_state_map, vertex_token_source_set,
    tp_vertex_batch_size, message_count, vertex_nlc_matches,
    vertex_active_edge_state_map, active_edge_target_vertex_degree_map);

  active_edge_target_vertex_degree_map.clear();

  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
  time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | Three Clique Edge State : "
      << time_end - time_start << std::endl;
  }

  do_nonlocal_constraint_checking = false; // Test 

  /////////////////////////////////////////////////////////////////////////////
//#ifdef ENABLE_BLOCK
  // synchronize edge state

  time_start = MPI_Wtime();

  prunejuice::indexing::synchronize_edge_state_batch<graph_type, Vertex, Edge, VertexData,
    EdgeData, VertexMetadata, EdgeMetadata, VertexActive,
    VertexUint8MapCollection, TemplateVertex, VertexStateMap, PatternGraph,
    /*PatternUtilities*/PatternNonlocalConstraint, VertexUint8Map, VertexSetCollection,
    DelegateGraphVertexDataSTDAllocator, Boolean, BitSet, VertexNonlocalConstraintMatches,
    EdgeStateCollection>
    (graph, vertex_metadata, vertex_active, vertex_active_edges_map,
    template_vertices, vertex_state_map, vertex_token_source_set,
    tp_vertex_batch_size/*mpi_size*/, message_count, vertex_nlc_matches, 
    vertex_active_edge_state_map);

  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
  time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | Synchronize Edge State : "
      << time_end - time_start << std::endl;
  }
//#endif
  // Test
  // average edge state size

  size_t index_edge_count = 0;
  size_t index_size = 0;
  for (vitr_type vitr = graph->vertices_begin();
    vitr != graph->vertices_end(); ++vitr) {
    vloc_type vertex = *vitr;
    index_edge_count+= vertex_active_edge_state_map[vertex].size();
    for (auto& n : vertex_active_edge_state_map[vertex]) {
      //std::cout << graph->locator_to_label(vertex) << " " << n.first << " : "; // << std::endl;
      //for (auto& i : n.second) {
      //  std::cout << i.first << " ";
      //}
      //std::cout << std::endl;
      index_size+=n.second.size();
    }
  }

  size_t global_index_edge_count = havoqgt::mpi_all_reduce(index_edge_count, std::plus<size_t>(), MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD);

  size_t global_index_size = havoqgt::mpi_all_reduce(index_size, std::plus<size_t>(), MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD);

  auto average_index_size_per_edge = global_index_size / global_index_edge_count;

  if (mpi_rank == 0) {
    std::cout << "Pattern Matching | Average Index Size Per Edge : " << average_index_size_per_edge << std::endl;
  }

  // Test

  //////////////////////////////////////////////////////////////////////////////

  // k-truss

  time_start = MPI_Wtime();

  size_t global_ktruss_count = prunejuice::indexing::ktruss_count
    <graph_type, VertexStateMap, EdgeStateCollection>
    (graph, vertex_state_map, vertex_active_edge_state_map, 2);

  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
  time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | K-Truss : "
      << time_end - time_start << std::endl;
  }

  if(mpi_rank == 0) {
    std::cout << "Pattern Matching | K-Truss Count : "
      << global_ktruss_count << std::endl;
  }

  //////////////////////////////////////////////////////////////////////////////

  // k-clique 

  time_start = MPI_Wtime();

  uint64_t global_kclique_count = prunejuice::indexing::kclique_count
    <graph_type, Vertex, VertexStateMap, EdgeStateCollection>
    (graph, vertex_state_map, vertex_active_edge_state_map,
    tp_vertex_batch_size);

  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
  time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | K-Clique : "
      << time_end - time_start << std::endl;
  }

  if(mpi_rank == 0) {
    std::cout << "Pattern Matching | K-Clique Count : "
      << global_kclique_count << std::endl;
  }

  //////////////////////////////////////////////////////////////////////////////
  
//#ifdef ENABLE_BLOCK
 
  // toekn passing

  double token_passing_time_start = MPI_Wtime(); 

  if (ptrn_util_two.input_patterns.size() < 1) {
    global_not_finished = false;
    do_nonlocal_constraint_checking = false; 
  }   

  // Test
  // forced token passing
  // Important : freezes in edit distance mode, do not enable 
  //if (global_itr_count == 0) {
    //global_not_finished = true; // TODO: for load balancing experiments, ? 
    //do_nonlocal_constraint_checking = true; 
  //}
  // Test  

  // TODO: code indentation
  //if ((token_passing_algo == 0) && global_not_finished) { // do token passing ?
  if (do_nonlocal_constraint_checking && global_not_finished) { // TODO: do we need this? 

  global_not_finished = false; // TODO: not sure if we need this here 

  //typedef std::unordered_map<Vertex, bool> TokenSourceMap; 
  //TokenSourceMap token_source_map;
  VertexUint8Map token_source_map; // per vertex state

  //std::vector<bool> pattern_found(ptrn_util_two.input_patterns.size(), false); 
  // TODO: bool does not work with mpi_all_reduce_inplace  
  //typedef std::vector<Boolean> VectorBoolean;  
  //std::vector<uint8_t> pattern_found(ptrn_util_two.input_patterns.size(), 0); 
  VectorBoolean pattern_found(ptrn_util_two.input_patterns.size(), 0); // per rank state
  VectorBoolean pattern_token_source_found(ptrn_util_two.input_patterns.size(), 0); // per rank state

  // TODO: code indentation
  // loop over the constraints and run token passing
  for (size_t pl = 0; pl < ptrn_util_two.input_patterns.size(); pl++) {

    // TODO: only output subgraphs when doing enumeration  
    // result
    std::string paths_result_filename = result_dir + //"/" +
    //std::to_string(ps) + "/all_ranks_paths/paths_" + 
    //std::to_string(ps) + 
    //std::string paths_result_filename = pattern_output_dir + // TODO: revert changes
      "/all_ranks_subgraphs/subgraphs_" +
    std::to_string(pl) + "_" + std::to_string(mpi_rank);
    std::ofstream paths_result_file(paths_result_filename, std::ofstream::out);

    bool token_source_deleted = false;

    // TODO: This is actually a bad design. It should be one object per entry. 
    // ptrn_util_twois the object 
 
  // setup pattern  
  auto pattern_tp = std::get<0>(ptrn_util_two.input_patterns[pl]); // VertexData
  auto pattern_indices_tp = std::get<1>(ptrn_util_two.input_patterns[pl]); // Vertex
  auto pattern_cycle_length_tp = std::get<2>(ptrn_util_two.input_patterns[pl]); // uint
  auto pattern_valid_cycle_tp = std::get<3>(ptrn_util_two.input_patterns[pl]); // boolean
//  auto pattern_interleave_label_propagation_tp = std::get<4>(ptrn_util_two.input_patterns[pl]); // boolean
//--  auto pattern_seleted_edges_tp = std::get<5>(ptrn_util_two.input_patterns[pl]); // boolean 
//  auto pattern_selected_vertices_tp = std::get<5>(ptrn_util_two.input_patterns[pl]); // boolean
  
  auto pattern_selected_vertices_tp = 0; // TODO: remove
  
  auto pattern_is_tds_tp = std::get<4>(ptrn_util_two.input_patterns[pl]); // boolean
  auto pattern_interleave_label_propagation_tp = std::get<5>(ptrn_util_two.input_patterns[pl]); // boolean
  auto pattern_constraint_tp = std::get<6>(ptrn_util_two.input_patterns[pl]); // uint

  auto pattern_enumeration_tp = ptrn_util_two.enumeration_patterns[pl]; 
  auto pattern_aggregation_steps_tp = ptrn_util_two.aggregation_steps[pl]; 

  // TODO: read from file / remove
  auto pattern_selected_edges_tp = false; // boolean
  auto pattern_mark_join_vertex_tp = false; // boolean
  auto pattern_ignore_join_vertex_tp = false; // boolean  
  size_t pattern_join_vertex_tp = 0; // TODO: 
  //bool do_tds_tp = false;

  message_count = 0;

  // Test
  // RDT_25
  //if (pl >= 4) {
  // /p/lscratchf/havoqgtu/reza2_tmp/WDC_patterns_12_tree_C_2 / C_4
  //if (pl >= 8) {
  // /p/lscratchf/havoqgtu/reza2_tmp/WDC_patterns_12_tree_C_2 - pruned graph
  //if (pl >= 0) {   
  // /p/lscratchf/havoqgtu/reza2_tmp/WDC_patterns_12_D
  //if (pl >= 4) {
  // /p/lscratchf/havoqgtu/reza2_tmp/WDC_patterns 
  //if (pl >= 8) {  
  // /p/lscratchf/havoqgtu/reza2_tmp/WDC_patterns_16/0
  //if (pl >= 4) {
  //if (pl >= 4) {
  // /p/lscratchf/havoqgtu/reza2_tmp/IMDB/graph/patterns_B_2/
  //if (pl >= 10 && pl <=27) {
  //if (pl >= 6) {
  //if (pl >= 0) {
  // RMAT_tree
  //if (pl >= 4) {
  //if (pl >= 99) { // Test
  //if (pattern_is_tds_tp) {
    //do_tds_tp = true;
    //if(mpi_rank == 0) {
    //  std::cout << "Token Passing [" << pl << "] | Template Driven Search " << std::endl;
    //}
  //}  
  // Test
  
  if(mpi_rank == 0) {
    std::cout << "Token Passing [" << pl << "] | Searching Subpattern : ";
    //pattern_util<VertexData>::output_pattern(pattern_tp);
    PatternNonlocalConstraint::output_pattern(pattern_tp);
    std::cout << "Token Passing [" << pl << "] | Vertices : ";
    //pattern_util<VertexData>::output_pattern(pattern_indices_tp);
    PatternNonlocalConstraint::output_pattern(pattern_indices_tp);
    std::cout << "Token Passing [" << pl << "] | Arguments : " 
      << pattern_cycle_length_tp << " " 
      << pattern_valid_cycle_tp << " " 
      << pattern_interleave_label_propagation_tp << " "
//--      << pattern_seleted_edges_tp << std::endl; // Test 		
      << pattern_selected_vertices_tp << std::endl; // Test

    std::cout << "Token Passing [" << pl << "] | Constraint ID : " 
      << pattern_constraint_tp << std::endl;    

    std::cout << "Token Passing [" << pl << "] | Enumeration Indices : ";
    //pattern_util<Vertex>::output_pattern(pattern_enumeration_tp); 
    PatternNonlocalConstraint::output_pattern(pattern_enumeration_tp);
    //std::cout << "Token Passing [" << pl << "] | Agreegation Steps : TODO" << std::endl;
    //PatternNonlocalConstraint::output_pattern(pattern_aggregation_steps_tp); // TODO:     
  }

  //return 0; // Test

  // initialize containers
  
  if (!pattern_selected_vertices_tp) {
    token_source_map.clear(); // Important
    vertex_token_source_set.clear(); // Important
    vertex_active_edge_state_map.clear();
  } else {
    // TODO: delete

    // delete invalid vertices from the token_source_map
    // reset the valid vertices in the token_source_map 

    /*for (auto itr = token_source_map.begin(); itr != token_source_map.end(); ) {
     if (!itr->second) {
       itr = token_source_map.erase(itr); // C++11
     } else {
       itr->second = 0;
       ++itr;
     }
    }*/
    token_source_map.clear(); // Important       
 
    // Test
    //if (token_source_map.size() > 0) {   
    //  std::cout << mpi_rank << " " << token_source_map.size() << std::endl;
    //}

    //for (auto& s : token_source_map) {
    //  if (pl == 3 && s.second) {
    //  std::cout << ">> >> Valid  " << pl << " " << mpi_rank << " " << s.first
    //    << " " << s.second << " " << vertex_metadata[graph->label_to_locator(s.first)]
    //    << " " << global_not_finished << std::endl;
    //  }
    //} 
    // Test

    // filter vertices, do not clear vertex_token_source_set for the vertices with label pattern_tp[0]      
    //if (mpi_rank == 0) { // Test
    //  std::cout << pattern_tp[0] << " " << pattern_indices_tp[0] << std::endl; // Test 
    //} // Test
    
    for (vitr_type vitr = graph->vertices_begin(); 
      vitr != graph->vertices_end(); ++vitr) {
      vloc_type vertex = *vitr;
      if (vertex_active[vertex] && 
        (vertex_metadata[vertex] == pattern_tp[pattern_tp.size() - 1])) {
        //std::cout << mpi_rank << " " << graph->locator_to_label(vertex) << " [l] " << vertex_token_source_set[vertex].size() << std::endl; // Test  
        continue; 
      } else {
        vertex_token_source_set[vertex].clear();
      }
    } 	
    	
    for(vitr_type vitr = graph->delegate_vertices_begin();
      vitr != graph->delegate_vertices_end(); ++vitr) {
      vloc_type vertex = *vitr;
      if (vertex_active[vertex] &&
        (vertex_metadata[vertex] == pattern_tp[pattern_tp.size() - 1])) {
        //std::cout << mpi_rank << " " << graph->locator_to_label(vertex) << " [d] " << vertex_token_source_set[vertex].size() << std::endl; // Test
        continue;
      } else {
        vertex_token_source_set[vertex].clear();
      } 
    }   
           
  } // if pattern_selected_vertices_tp
 
  MPI_Barrier(MPI_COMM_WORLD); // TODO: do we need this here?
 
//--  //token_source_edge_set.clear(); // Important // edge aware

  //continue; // Test 
    
  time_start = MPI_Wtime();

  // token_passing_pattern_matching.hpp
  //token_passing_pattern_matching(graph, vertex_metadata, pattern_tp, 
  //  pattern_indices_tp, vertex_rank, pattern_graph, vertex_state_map, 
  //  token_source_map, pattern_cycle_length_tp, pattern_valid_cycle_tp, pattern_found[pl], *edge_data_ptr, vertex_token_source_set);
  
//--  if (!pattern_seleted_edges_tp) { 
//--    token_source_edge_set.clear(); // edge aware 
//--  }

#ifdef TP_ASYNC
  // token_passing_pattern_matching_new.hpp
//  token_passing_pattern_matching(graph, vertex_metadata, pattern_tp,
//    pattern_indices_tp, vertex_rank, pattern_graph, vertex_state_map,
//    token_source_map, pattern_cycle_length_tp, pattern_valid_cycle_tp, pattern_seleted_edges_tp, 
//    pattern_found[pl], edge_active, /**edge_data_ptr,*/ vertex_token_source_set, token_source_edge_set, vertex_active);

  //if (do_tds_tp) {
  if (pattern_is_tds_tp) {
    if (do_subgraph_indexing) {
      if(mpi_rank == 0) {  
        std::cout << "Token Passing [" << pl << "] | Subgraph Indexing ... " << std::endl;
      }

      prunejuice::indexing::token_passing_pattern_matching<graph_type, Vertex, Edge, VertexData,
        EdgeData, VertexMetadata, EdgeMetadata, VertexActive,
        VertexUint8MapCollection, TemplateVertex, VertexStateMap, PatternGraph,
        /*PatternUtilities*/PatternNonlocalConstraint, VertexUint8Map, VertexSetCollection,
        DelegateGraphVertexDataSTDAllocator, Boolean, BitSet, VertexNonlocalConstraintMatches, 
        EdgeStateCollection>
        (graph, vertex_metadata, vertex_active, vertex_active_edges_map,
        template_vertices, vertex_state_map, pattern_graph, ptrn_util_two, pl,
        token_source_map, vertex_token_source_set,
        pattern_found[pl], tp_vertex_batch_size, paths_result_file, message_count,
        pattern_constraint_tp, vertex_nlc_matches, vertex_active_edge_state_map);
  
    } else {  
    // token_passing_pattern_matching_nonunique_tds_batch_1.hpp
    // token_passing_pattern_matching_nonunique_iterative_tds_1.hpp
    // token_passing_pattern_matching_nonunique_iterative_tds_batch_1.hpp
    // token_passing_pattern_matching_nonunique_tds_batch_1.hpp 
    prunejuice::token_passing_pattern_matching<graph_type, Vertex, Edge, VertexData, 
      EdgeData, VertexMetadata, EdgeMetadata, VertexActive, 
      VertexUint8MapCollection, TemplateVertex, VertexStateMap, PatternGraph, 
      /*PatternUtilities*/PatternNonlocalConstraint, VertexUint8Map, VertexSetCollection, 
      DelegateGraphVertexDataSTDAllocator, Boolean, BitSet, VertexNonlocalConstraintMatches>
      (graph, vertex_metadata, vertex_active, vertex_active_edges_map, 
      template_vertices, vertex_state_map, pattern_graph, ptrn_util_two, pl,
      token_source_map, vertex_token_source_set, 
      pattern_found[pl], tp_vertex_batch_size, paths_result_file, message_count, 
      pattern_constraint_tp, vertex_nlc_matches);
    // token_passing_pattern_matching_nonunique_tds_1.hpp
    /*token_passing_pattern_matching<graph_type, Vertex, VertexMetaData, 
      decltype(pattern_tp), decltype(pattern_indices_tp), decltype(pattern_enumeration_tp),
      uint8_t, PatternGraph, VertexStateMap, VertexUint8Map, edge_data_t,
      VertexSetCollection, VertexActive, TemplateVertex, VertexUint8MapCollection, BitSet>
      (graph, vertex_metadata, pattern_tp, pattern_indices_tp, pattern_enumeration_tp, 
      vertex_rank, pattern_graph, vertex_state_map,
      token_source_map, pattern_cycle_length_tp, pattern_valid_cycle_tp,
      pattern_found[pl], *edge_data_ptr, vertex_token_source_set, vertex_active,
      template_vertices, vertex_active_edges_map, pattern_selected_vertices_tp, 
      paths_result_file, message_count); // pass a boolean flag to indicate to use batching*/
    } 
  } else {     
    prunejuice::token_passing_pattern_matching<graph_type, VertexMetadata, 
    decltype(pattern_tp), decltype(pattern_indices_tp), uint8_t, PatternGraph,
    VertexStateMap, VertexUint8Map, edge_data_t,
    VertexSetCollection, VertexActive, TemplateVertex, VertexUint8MapCollection, 
    BitSet, VertexNonlocalConstraintMatches>(graph, vertex_metadata, pattern_tp,
    pattern_indices_tp, vertex_rank, pattern_graph, vertex_state_map,
    token_source_map, pattern_cycle_length_tp, pattern_valid_cycle_tp,
    pattern_found[pl], *edge_data_ptr, vertex_token_source_set, vertex_active, 
    template_vertices, vertex_active_edges_map, pattern_selected_vertices_tp, //);
    pattern_selected_edges_tp, pattern_mark_join_vertex_tp,
    pattern_ignore_join_vertex_tp, pattern_join_vertex_tp, message_count, 
    pattern_constraint_tp, vertex_nlc_matches);
  } 
#endif

// TODO: delete
//#ifdef TP_BATCH
  // token_passing_pattern_matching_batch.hpp
//  token_passing_pattern_matching(graph, vertex_metadata, pattern_tp,
//    pattern_indices_tp, vertex_rank, pattern_graph, vertex_state_map,
//    token_source_map, pattern_cycle_length_tp, pattern_valid_cycle_tp,
//    pattern_found[pl], *edge_data_ptr, vertex_token_source_set, vertex_active, 
//    global_not_finished, token_source_deleted); 
//#endif 
 
  MPI_Barrier(MPI_COMM_WORLD); // TODO: do we need this here?    
  time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | Token Passing (Traversal) [" 
      << pl << "] : " << time_end - time_start << std::endl;
  }

  // result
//--  if(mpi_rank == 0) {
//--    superstep_result_file << global_itr_count << ", TP, "
//--      << pl << ", "
//--      << time_end - time_start << "\n";      
//--  }

//  } // for - loop over the patterns

  // pattren found ? // TODO: update for the new token passing loop
//  havoqgt::mpi::mpi_all_reduce_inplace(pattern_found, std::greater<uint8_t>(), MPI_COMM_WORLD);
//  MPI_Barrier(MPI_COMM_WORLD); // TODO: do we need this here?   
//  if(mpi_rank == 0) {   
//    for (size_t pl = 0; pl < ptrn_util_two.input_patterns.size(); pl++) {
//      std::string s = pattern_found[pl] == 1 ? "true" : "false";
//      std::cout << "Token Passing [" << pl << "] | Found pattern : " << s << std::endl;
//    }
//  }

#ifdef TP_ASYNC

  // remove the invalid (token source) vertices from the vertex_state_map
  // for delegates, set vertex_active to false

  // TODO: In the case, a vertex is on multiple cycles/chains (not as the token source)
  // only invalidate it as a token source, but do not remove it from the vertex_state_map

  uint64_t remove_count = 0;      
  size_t token_source_pattern_indices_tp_index = 0; // TODO: this is confusing, update 
  
  bool is_token_source_map_not_empty = havoqgt::mpi_all_reduce
    (!token_source_map.empty(), std::greater<uint8_t>(), MPI_COMM_WORLD); // TODO: less did not work?
  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here

  pattern_token_source_found[pl] = is_token_source_map_not_empty;

//  if (pattern_selected_vertices_tp) {
    // TODO: delete
//    token_source_pattern_indices_tp_index = pattern_indices_tp.size() - 1; // Important     
    //std::cout << "token_source_map.size() " << token_source_map.size() << std::endl; // Test
//  }  

  for (auto& s : token_source_map) {
    auto v_locator = graph->label_to_locator(s.first);
    if (!s.second) {
      //if (pl == 2) { // Test
      //  std::cout << "Invalid  " << pl << " " << mpi_rank << " " << s.first 
      //    << " " << s.second << " " << global_not_finished << std::endl;
      //} // Test 
      //if (pl <= 1) { // Test
      //  std::cout << "Invalid  " << pl << " " << mpi_rank << " " << s.first
      //    << " " << s.second << std::endl;  
      //} //Test

      //auto v_locator = graph->label_to_locator(s.first);
      BitSet v_template_vertices(template_vertices[v_locator]);

      if (v_template_vertices.none()) {
        continue;
      } 

      //pattern_indices_tp[0]; // token source template vertex ID

      if (v_template_vertices.test(pattern_indices_tp[token_source_pattern_indices_tp_index])) {
        assert(pattern_indices_tp[token_source_pattern_indices_tp_index] < max_template_vertex_count); // Test
        v_template_vertices.reset(pattern_indices_tp[token_source_pattern_indices_tp_index]);
        template_vertices[v_locator] = v_template_vertices.to_ulong();
      }

      if (v_template_vertices.none()) {
        vertex_active[graph->label_to_locator(s.first)] = false;
      }

      if (!global_not_finished) { // TODO: not sure if we need this here
        global_not_finished = true;
      }
 
      if (!token_source_deleted) {  
	token_source_deleted = true;
      }

      //if (global_itr_count > 0) { // Test
      //      //  std::cout << "MPI Rank: " << mpi_rank << " template vertices " << v_template_vertices << " global_not_finished " << global_not_finished << std::endl;
      //}  
    } else {
      // set the constraint(s) matches for this vertex 
//~~~      if (ps == 0) { // Test
//~~~        assert(pattern_constraint_tp < vertex_nlc_matches[v_locator].size());
        if (!vertex_nlc_matches[v_locator].test(pattern_constraint_tp) && pattern_constraint_tp < 6) {
          //std::cout << pattern_constraint_tp << " Set. "; // Test
//~~~          vertex_nlc_matches[v_locator].set(pattern_constraint_tp); 
        }
//~~~      } // Test
    }  
    
    // Test
    //else { 
    //  if (pl == 2) {
    //    std::cout << "Valid  " << pl << " " << mpi_rank << " " << s.first
    //      << " " << s.second << " " << vertex_metadata[graph->label_to_locator(s.first)] 
    //      << " " << global_not_finished << std::endl;
    //  }   
    //}
    // Test
  }

  MPI_Barrier(MPI_COMM_WORLD); // TODO: do we need this here? // New

  vertex_active.all_min_reduce(); 
  MPI_Barrier(MPI_COMM_WORLD); // TODO: do we need this here?

  // TODO: this is a temporary patch, forcing all the delegates to have no identity
  for(vitr_type vitr = graph->delegate_vertices_begin();
    vitr != graph->delegate_vertices_end(); ++vitr) {
    auto vertex = *vitr;
    if (vertex.is_delegate() && (graph->master(vertex) == mpi_rank)) {
      continue;  // skip the controller
    }
    else {
      auto find_vertex = vertex_state_map.find(graph->locator_to_label(vertex));
      if (find_vertex == vertex_state_map.end()) {
        template_vertices[vertex] = 0;
      }
    }
  }
  MPI_Barrier(MPI_COMM_WORLD);

  template_vertices.all_max_reduce(); // ensure all the delegates have the same value as the controller
  MPI_Barrier(MPI_COMM_WORLD); // TODO: do we need this here?

  // remove from vertex_state_map
  for (auto& s : token_source_map) {
    auto v_locator = graph->label_to_locator(s.first);
    if (!vertex_active[v_locator]) {
      auto find_vertex = 
        vertex_state_map.find(s.first);
 
      if (find_vertex != vertex_state_map.end()) { 
      
        if (vertex_state_map.erase(s.first) < 1) { // s.first is the vertex
          std::cerr << "Error: failed to remove an element from the map."
          << std::endl;
        } else {
          remove_count++;
          //if (!global_not_finished) {
          //  global_not_finished = true;
          //}
        }  
 
      }
    }

    // Test 
    //if (pl == 2 && s.second) {
    //  std::cout << ">> Valid  " << pl << " " << mpi_rank << " " << s.first
    //    << " " << s.second << " " << vertex_metadata[graph->label_to_locator(s.first)] 
    //    << " " << global_not_finished << std::endl;
    //}
    // Test 
  }

//  if(mpi_rank == 0) {
//    std::cout << "Token Passing [" << pl << "] | MPI Rank [" << mpi_rank 
//      << "] | Removed " << remove_count << " vertices."<< std::endl; // TODO: not useful informtion
//  }

#endif // ifdef TP_ASYNC

  time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | Token Passing [" << pl 
      << "] : " << time_end - time_start << std::endl;
  }

  // result
  if(mpi_rank == 0) {
    superstep_result_file << global_itr_count << ", TP, "
      << pl << ", "
      << time_end - time_start << "\n";
  }
  
  if(mpi_rank == 0) {
    step_result_file << global_itr_count << ", TP, "
      << pl << ", "
      << time_end - time_start << "\n";
  }

  // Important : This may slow down things - only for presenting results
  active_vertices_count = 0;
  active_edges_count = 0;   

  for (auto& v : vertex_state_map) {
    auto v_locator = graph->label_to_locator(v.first);
    if (v_locator.is_delegate() && (graph->master(v_locator) == mpi_rank)) {
      active_vertices_count++;

      // edges
      active_edges_count+=vertex_active_edges_map[v_locator].size();   
    } else if (!v_locator.is_delegate()) {
      active_vertices_count++;

      // edges
      active_edges_count+=vertex_active_edges_map[v_locator].size();   
    }
  }

  // vertices
  active_vertices_count_result_file << global_itr_count << ", TP, "
    << pl << ", "  
    << active_vertices_count << "\n";  

  // edges   
  active_edges_count_result_file << global_itr_count << ", TP, "
    << pl << ", "
    << active_edges_count << "\n";

  // messages
  message_count_result_file << global_itr_count << ", TP, "
    << pl << ", "
    << message_count << "\n";

  // result
  
  MPI_Barrier(MPI_COMM_WORLD); // TODO: do we need this here? // New

  if (is_token_source_map_not_empty) {

  //if (pl <= 1 && pattern_found[pl] == 1) { // Test
  //  std::cout << mpi_rank << " | Token Passing [" << pl << "] | Found Pattern : " << pattern_found[pl] << std::endl;             
  //} // Test    

  // pattren found ? // TODO: write output to file 
  ///havoqgt::mpi::mpi_all_reduce_inplace(pattern_found, std::greater<uint8_t>(), MPI_COMM_WORLD);
  havoqgt::mpi_all_reduce_inplace(pattern_found, std::greater<uint8_t>(), MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD); // TODO: do we need this here?   
  if(mpi_rank == 0) {   
//    for (size_t pl = 0; pl < ptrn_util_two.input_patterns.size(); pl++) {
      std::string s = pattern_found[pl] == 1 ? "True" : "False";
      std::cout << "Token Passing [" << pl << "] | Found Subpattern : " << s << std::endl;
//    }
  }

  MPI_Barrier(MPI_COMM_WORLD); // TODO: do we need this here? // New

  // verify global token source deleted status
  //token_source_deleted = havoqgt::mpi::mpi_all_reduce(token_source_deleted, std::logical_or<bool>(), MPI_COMM_WORLD); // does not work  
  token_source_deleted = havoqgt::mpi_all_reduce(token_source_deleted, std::greater<uint8_t>(), MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here 
  if(mpi_rank == 0) {
    std::cout << "Token Passing [" << pl << "] | Token Source Deleted Status : ";
    if (token_source_deleted) {
      std::cout << "Deleted" << std::endl;
    } else {
      std::cout << "Not Deleted" << std::endl;
    }
  } 

  } else {
    if(mpi_rank == 0) {
      std::cout << "Token Passing [" << pl << "] | No Token Source Found" << std::endl;   
    }
  } // is_token_source_map_not_empty

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
  
  // interleave token passing with label propagation 

  if (token_source_deleted && pattern_interleave_label_propagation_tp) { // TODO: first check if there are active vertices ? 

  bool global_not_finished_dummy = true; // TODO: do we need this?

  // lable propagation   
  label_propagation_time_start = MPI_Wtime();

  // label propagation pattern matching bsp, iterative 
//  label_propagation_pattern_matching_bsp<graph_type, VertexMetaData, VertexData, decltype(pattern), decltype(pattern_indices), 
//    /*VertexRank*/uint8_t, VertexActive, /*VertexIteration*/uint8_t, VertexStateMap, PatternGraph, EdgeActive, VertexSetCollection,
//    EdgeMetadata>
//    (graph, vertex_metadata, pattern, pattern_indices, vertex_rank, vertex_active, 
//    vertex_iteration, vertex_state_map, pattern_graph, global_initstep, global_not_finished_dummy, 
//    global_itr_count, superstep_result_file, active_vertices_count_result_file, edge_active/**edge_data_ptr*/, edge_metadata);
#ifdef LCC_OLD 
  prunejuice::label_propagation_pattern_matching_bsp<graph_type, VertexMetadata, VertexData, decltype(pattern), decltype(pattern_indices),
    /*VertexRank*/uint8_t, VertexActive, /*VertexIteration*/uint8_t, VertexStateMap, PatternGraph, BitSet, TemplateVertex, 
    VertexUint8MapCollection>
    (graph, vertex_metadata, pattern, pattern_indices, vertex_rank, vertex_active,
    vertex_iteration, vertex_state_map, pattern_graph, global_init_step, global_not_finished,
    global_itr_count, superstep_result_file, active_vertices_count_result_file, active_edges_count_result_file,
    template_vertices, vertex_active_edges_map, message_count_result_file);
#endif

  prunejuice::label_propagation_pattern_matching_bsp<Vertex, VertexData, 
    graph_type, VertexMetadata, VertexStateMap, VertexActive, 
    VertexUint8MapCollection, TemplateVertexBitSet, TemplateVertex, PatternGraph>
    (graph, vertex_metadata, vertex_state_map, vertex_active, 
    vertex_active_edges_map, template_vertices, pattern_graph, global_init_step, 
    global_not_finished, global_itr_count, superstep_result_file, 
    active_vertices_count_result_file, active_edges_count_result_file,
    message_count_result_file);  

  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
  label_propagation_time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | Local Constraint Checking (Interleaved) : " 
      << label_propagation_time_end - label_propagation_time_start << std::endl;
  }

  // result
  if(mpi_rank == 0) {
    step_result_file << global_itr_count << ", LP, " << (pl + 1) << ", "   
      << (label_propagation_time_end - label_propagation_time_start) << "\n";
  }

  /////////////////////////////////////////////////////////////////////////////

  //if (global_init_step) {
  //  global_init_step = false;
  //} 

  // verify global termination condition
  //std::cout << "Fuzzy Pattern Matching | Global Not Finished status (local) : " << global_not_finished << std::endl; // Test
  // global_not_finished = havoqgt::mpi::mpi_all_reduce(global_not_finished, std::logical_or<bool>(), MPI_COMM_WORLD); // does not work
//--  global_not_finished = havoqgt::mpi::mpi_all_reduce(global_not_finished, std::greater<uint8_t>(), MPI_COMM_WORLD); 
//--  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here 

//--  if(mpi_rank == 0) {
//--    std::cout << "Fuzzy Pattern Matching | Global Finished Status : "; 
//--    if (global_not_finished) { 
//--      std::cout << "Continue" << std::endl;
//--    } else {
//--      std::cout << "Stop" << std::endl;
//--    } 
//--  }

  // global verification - are all vertex_state_maps empty
  // false - no active vertex left, true - active vertices left 
//--  global_active_vertex = true; //vertex_state_map.size() < 1 ? false : true;
//--  if (vertex_state_map.size() < 1) {
//    global_active_vertex = false;
//--  }

  // TODO: What is the issue here? Preventing stdout from this file beyond this point.  	
  // global_active_vertex = havoqgt::mpi::mpi_all_reduce(global_active_vertex, std::greater<uint8_t>(), MPI_COMM_WORLD); 
  // TODO: not working properly - why? // bool does not work
  //MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here

//  global_not_finished = global_active_vertex; // TODO: verify and fix

//--  if(mpi_rank == 0) {
//--    std::cout << "Fuzzy Pattern Matching | Global Active Vertex Status : ";
//--    if (global_active_vertex) {
//--      std::cout << "Active vertices left." << std::endl;
//--    } else {
//--      std::cout << "No active vertex left." << std::endl;
//--    }
//--  }

  //////////////////////////////////////////////////////////////////////////////

  // global termination detection

  // Test
  // global active vertices count
  size_t global_active_vertices_count =  
    havoqgt::mpi_all_reduce(vertex_state_map.size(),
    std::greater<size_t>(), MPI_COMM_WORLD);   
  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
  if (global_active_vertices_count < 1) {
    global_not_finished = false;
    do_nonlocal_constraint_checking = false;
    break;
  }
  // Test

  //////////////////////////////////////////////////////////////////////////////
   	  
  } else { // if (token_source_deleted) // if (global_not_finished) - old code
    if(mpi_rank == 0) {
      std::cout << "Pattern Matching | Skipping Local Constraint Checking (Interleaved)" << std::endl;
    }        
  } // interleave token passing with label propagation

  //////////////////////////////////////////////////////////////////////////////
#ifdef ENABLE_BLOCK
  // synchronize edge state

  time_start = MPI_Wtime();

  prunejuice::indexing::synchronize_edge_state_batch<graph_type, Vertex, Edge, VertexData,
    EdgeData, VertexMetadata, EdgeMetadata, VertexActive,
    VertexUint8MapCollection, TemplateVertex, VertexStateMap, PatternGraph,
    /*PatternUtilities*/PatternNonlocalConstraint, VertexUint8Map, VertexSetCollection,
    DelegateGraphVertexDataSTDAllocator, Boolean, BitSet, VertexNonlocalConstraintMatches,
    EdgeStateCollection>
    (graph, vertex_metadata, vertex_active, vertex_active_edges_map,
    template_vertices, vertex_state_map, pattern_graph, ptrn_util_two, pl,
    token_source_map, vertex_token_source_set,
    pattern_found[pl], /*tp_vertex_batch_size*/mpi_size, paths_result_file, message_count,
    pattern_constraint_tp, vertex_nlc_matches, vertex_active_edge_state_map);

  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
  time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | Synchronize Edge State : "
      << time_end - time_start << std::endl;
  }
#endif
  //////////////////////////////////////////////////////////////////////////////

  // k-truss

  time_start = MPI_Wtime();

  size_t global_ktruss_count = prunejuice::indexing::ktruss_count
    <graph_type, VertexStateMap, EdgeStateCollection> 
    (graph, vertex_state_map, vertex_active_edge_state_map, 2);
  
  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
  time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | K-Truss : "
      << time_end - time_start << std::endl;
  }

  if(mpi_rank == 0) {
    std::cout << "Pattern Matching | K-Truss Count : "
      << global_ktruss_count << std::endl;
  } 

  //////////////////////////////////////////////////////////////////////////////

  // k-clique 

  time_start = MPI_Wtime();

  uint64_t global_kclique_count = prunejuice::indexing::kclique_count
    <graph_type, Vertex, VertexStateMap, EdgeStateCollection>
    (graph, vertex_state_map, vertex_active_edge_state_map, 
    tp_vertex_batch_size);

  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here
  time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | K-Clique : "
      << time_end - time_start << std::endl;
  }

  if(mpi_rank == 0) {
    std::cout << "Pattern Matching | K-Clique Count : "
      << global_kclique_count << std::endl;
  }

  //////////////////////////////////////////////////////////////////////////////

  // result
  paths_result_file.close();

  MPI_Barrier(MPI_COMM_WORLD); // TODO: do we need this here? // New 

  //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

  } // for - loop over the constraints and run token passing 

  // pattren found ? // TODO: write output to file 
  ///havoqgt::mpi::mpi_all_reduce_inplace(pattern_found, std::greater<uint8_t>(), MPI_COMM_WORLD);
  havoqgt::mpi_all_reduce_inplace(pattern_found, std::greater<uint8_t>(), MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD); // TODO: do we need this here?   
  if(mpi_rank == 0) {   
    for (size_t pl = 0; pl < ptrn_util_two.input_patterns.size(); pl++) {
      if (pattern_token_source_found[pl]) {
        std::string s = pattern_found[pl] == 1 ? "True" : "False";
        std::cout << "Token Passing [" << pl << "] | Found Subpattern : " << s << std::endl;
      } else {
        std::cout << "Token Passing [" << pl << "] | No Token Source Found" << std::endl;
      }
    }
  }

  /////////////////////////////////////////////////////////////////////////////

  // result
  // Important : This may slow down things - only for presenting results
//--  active_vertices_count = 0;
//--  for (auto& v : vertex_state_map) {
//--    auto v_locator = graph->label_to_locator(v.first);
//--    if (v_locator.is_delegate() && (graph->master(v_locator) == mpi_rank)) {
//--      active_vertices_count++;
//--    } else if (!v_locator.is_delegate()) {
//--      active_vertices_count++;
//--    }
//--  }

//--  active_vertices_count_result_file << global_itr_count << ", TP, "
//--    << "0, "  
//--    << active_vertices_count << "\n";    
 
  } else {
    if(mpi_rank == 0) { 
      std::cout << "Pattern Matching | Skipping Token Passing" << std::endl;
    }
    global_not_finished = false;
  } // do token passing ?

  // done token passing // TODO: not useful anymore 
  //double token_passing_time_end = MPI_Wtime();
  //if(mpi_rank == 0) {
  //  std::cout << "Fuzzy Pattern Matching Time | Token Passing : " 
  //    << (token_passing_time_end - token_passing_time_start) << std::endl;
  //}

  // result // TODO: not useful anymore
  //if(mpi_rank == 0) {
  //  step_result_file << global_itr_count << ", TP, "  
  //    << (token_passing_time_end - token_passing_time_start) << "\n"; 
  //}

//#endif 

  ///////////////////////////////////////////////////////////////////////////// 

  // global termination detection  
  //std::cout << "Fuzzy Pattern Matching | Global Not Finished status (local) : " << global_not_finished << std::endl; // Test
  //global_not_finished = havoqgt::mpi::mpi_all_reduce(global_not_finished, std::logical_or<bool>(), MPI_COMM_WORLD); // does not work  
  ///global_not_finished = havoqgt::mpi::mpi_all_reduce(global_not_finished, std::greater<uint8_t>(), MPI_COMM_WORLD);
  global_not_finished = havoqgt::mpi_all_reduce(global_not_finished, std::greater<uint8_t>(), MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD); // TODO: might not need this here 

  if(mpi_rank == 0) {
    std::cout << "Pattern Matching | Global Finished Status : ";
    if (global_not_finished) {
      std::cout << "Continue" << std::endl;
    } else {
      std::cout << "Stop" << std::endl;
    }
  }

  /////////////////////////////////////////////////////////////////////////////

  double itr_time_end = MPI_Wtime(); 
//  if(mpi_rank == 0) { //TODO: sum of LCC and NLCC iterations
//    std::cout << "Pattern Matching Time | Pattern [" << ps 
//      << "] | Iteration [" << global_itr_count << "] : " 
//      << itr_time_end - itr_time_start << std::endl;  
//  }
 
  // result 
  if(mpi_rank == 0) {
    // iteration number, time
    itr_result_file << global_itr_count << ", "
      << (itr_time_end - itr_time_start) << "\n";
  }

  global_itr_count++; //TODO: sum of LCC and NLCC iterations

  // global termination
  // Test
  //if (global_itr_count > 0) {
  //  global_not_finished = false;
  //}
  //MPI_Barrier(MPI_COMM_WORLD);
  // Test

///  } while (global_not_finished); // application loop
 
  /////////////////////////////////////////////////////////////////////////////

  MPI_Barrier(MPI_COMM_WORLD);  
  double pattern_time_end = MPI_Wtime();
  if(mpi_rank == 0) {
    std::cout << "Pattern Matching Time | Pattern [" << current_edit_distance << "][" << ps << "] : " 
      << pattern_time_end - pattern_time_start << std::endl;
  }
   
//  if(mpi_rank == 0) { //TODO: sum of LCC and NLCC iterations
//    std::cout << "Pattern Matching | Pattern [" << ps 
//      << "] | # Iterations : " << global_itr_count << std::endl;
//  }

  // TODO: state that if the pattern was found or not  
 
  /////////////////////////////////////////////////////////////////////////////

  // result
   
  if(mpi_rank == 0) {  
    // pattern set element ID, number of ranks, 
    // total number of iterations (lp+tp), time, 
    // #edges in the pattern, #vertices in the pattern,  
    // #token passing paths
    pattern_set_result_file << ps << ", " 
      << mpi_size << ", "
      << global_itr_count << ", " 
      << (pattern_time_end - pattern_time_start) << ", "
      << pattern_graph.edge_count << ", " 
      << pattern_graph.vertex_count << ", "
      << ptrn_util_two.input_patterns.size() << "\n"; 
  }

  // Important : This may slow things down - only for presenting results

  size_t active_edge_count_local = 0;

  for (auto& v : vertex_state_map) {
    auto v_locator = graph->label_to_locator(v.first);
    if (v_locator.is_delegate() && (graph->master(v_locator) == mpi_rank)) {
      BitSet v_template_vertices(template_vertices[v_locator]); 
      active_vertices_result_file << mpi_rank << ", "
        << v.first << ", " 
        //<< v.second.vertex_pattern_index << ", "
        << "0, "
        << vertex_metadata[v_locator] << ", "
        << v_template_vertices << "\n";
     
      // edges 
      for (auto& n : vertex_active_edges_map[v_locator]) { 	 
        active_edges_result_file << mpi_rank << ", "
          << v.first << ", "
          << n.first //<< ", "
          //<< (size_t)n.second << ", "
          //<< vertex_active_edges_map[v_locator].size() 
          << "\n";
      }

      active_edge_count_local += vertex_active_edges_map[v_locator].size();
      k_edit_distance_vertex_set.insert(v.first); // Test
  
      // pattern neighbor
      if (vertex_active_edge_state_map[v_locator].size() > 0) {
        pattern_neighbors_result_file << v.first << " : ";
        for (auto& n : vertex_active_edge_state_map[v_locator]) {
          pattern_neighbors_result_file << n.first << " ";
          for (auto& w : n.second) {
            pattern_neighbors_result_file << w.first << ", ";
          }
          pattern_neighbors_result_file << " | ";
        }
        pattern_neighbors_result_file << "\n";
      } 

    } else if (!v_locator.is_delegate()) {
      BitSet v_template_vertices(template_vertices[v_locator]);
      active_vertices_result_file << mpi_rank << ", "
        << v.first << ", "
        //<< v.second.vertex_pattern_index << ", "
        << "0, "
        << vertex_metadata[v_locator] << ", "
        << v_template_vertices << "\n";

      // edges
      for (auto& n : vertex_active_edges_map[v_locator]) {  
        active_edges_result_file << mpi_rank << ", "
          << v.first << ", "
          << n.first //<< ", "          
          //<< (size_t)n.second << ", " 
          //<< vertex_active_edges_map[v_locator].size()
          << "\n"; 
      } 

      active_edge_count_local += vertex_active_edges_map[v_locator].size();  
      k_edit_distance_vertex_set.insert(v.first); // Test

      // pattern neighbor
      if (vertex_active_edge_state_map[v_locator].size() > 0) {
        pattern_neighbors_result_file << v.first << " : ";
        for (auto& n : vertex_active_edge_state_map[v_locator]) {
          pattern_neighbors_result_file << n.first << " ";
          for (auto& w : n.second) {
            pattern_neighbors_result_file << w.first << ", ";
          }
          pattern_neighbors_result_file << " | ";
        }
        pattern_neighbors_result_file << "\n";
      }
 
    }
  }

  // Test
  MPI_Barrier(MPI_COMM_WORLD); 
  size_t vertex_state_map_set_size_global = 
    havoqgt::mpi_all_reduce(vertex_state_map.size(), std::plus<size_t>(), MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD);
  size_t active_edge_count_global =
    havoqgt::mpi_all_reduce(active_edge_count_local, std::plus<size_t>(), MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD); 
 
  if (mpi_rank == 0) {
    std::cout << "Pattern Matching | Pattern Vertex Match Count | Pattern [" << current_edit_distance << "][" << ps << "] : " 
      << vertex_state_map_set_size_global << std::endl;
    std::cout << "Pattern Matching | Pattern Edge Match Count | Pattern [" << current_edit_distance << "][" << ps << "] : "
      << active_edge_count_global << std::endl;
  }
  // Test

  // cleanup memeory
  //vertex_rank.clear(); // TODO: add clear() method to vertex_data.cpp   
  //vertex_active.clear(); // TODO: add clear() method to vertex_data.cpp
  //vertex_state_map.clear();

  /////////////////////////////////////////////////////////////////////////////

    // close files
    itr_result_file.close();
    step_result_file.close();
    superstep_result_file.close();
    active_vertices_count_result_file.close(); 
    active_vertices_result_file.close();
    active_edges_count_result_file.close();
    active_edges_result_file.close();
    //paths_result_file.close();
    message_count_result_file.close();
    pattern_neighbors_result_file.close();

    MPI_Barrier(MPI_COMM_WORLD);

    // end of an elemnet in the pattern set
  } // for - loop over pattern set // loop over k-edit distance prototypes

  //application_time_end = MPI_Wtime();
  edit_distance_time_end = MPI_Wtime();
  if (mpi_rank == 0) {    
    std::cout << "Pattern Matching Time | Edit Distance [" << pk << "] : "
      << edit_distance_time_end - edit_distance_time_start << std::endl;
  }

  // Test
  // k-edit distance output 
  MPI_Barrier(MPI_COMM_WORLD); 
  size_t k_edit_distance_vertex_set_size_global = 
    havoqgt::mpi_all_reduce(k_edit_distance_vertex_set.size(), 
    std::plus<size_t>(), MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD);
  
  if (mpi_rank == 0) {
    //std::cout << "Pattern Matching | Global Vertex Match Count : " << k_edit_distance_vertex_set_size_global << std::endl;
    std::cout << "Pattern Matching | Edit Distance Vertex Match Count | Edit Distance [" << pk
      << "] : " << k_edit_distance_vertex_set_size_global << std::endl;
  }

  k_edit_distance_vertex_set.clear(); // Test

  all_edit_distance_vertex_set_size_global+=k_edit_distance_vertex_set_size_global;

    // vertex_nlc_matches
    /*for (auto& v : vertex_state_map) {
      auto v_locator = graph->label_to_locator(v.first);
      if (!vertex_nlc_matches[v_locator].none()) {
        std::cout << vertex_nlc_matches[v_locator] << std::endl;     
      }
    }*/
    // Test

  ////////////////////////////////////////////////////////////////////////////// 

  } // for - loop over up to k-edit distance prototypes

  //////////////////////////////////////////////////////////////////////////////
 
  MPI_Barrier(MPI_COMM_WORLD); 
  application_time_end = MPI_Wtime();
  if (mpi_rank == 0) {    
    std::cout << "Pattern Matching Time | Application : "
      << application_time_end - application_time_start << std::endl;
  }

  // Test
  /*MPI_Barrier(MPI_COMM_WORLD); 
  size_t k_edit_distance_vertex_set_size_global = 
    havoqgt::mpi_all_reduce(k_edit_distance_vertex_set.size(), std::plus<size_t>(), MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD);
  
  if (mpi_rank == 0) {
    std::cout << "Pattern Matching | Global Vertex Match Count : " << k_edit_distance_vertex_set_size_global << std::endl;
  }*/

  if (mpi_rank == 0) {
    std::cout << "Pattern Matching | Global Vertex Match Count : " << all_edit_distance_vertex_set_size_global << std::endl;
  }

  /*for (auto& v : vertex_state_map) {
    auto v_locator = graph->label_to_locator(v.first);
    if (!vertex_nlc_matches[v_locator].none()) {
      std::cout << vertex_nlc_matches[v_locator] << std::endl;     
    }
  }*/
  // Test

    if (mpi_rank == 0) {
      pattern_set_result_file.close(); // close file
      std::cout << "Pattern Matching | Done" << std::endl;
    }

    ////////////////////////////////////////////////////////////////////////////

  } // pattern matching  

  } // havoqgt_init
  ;
  // END Main MPI
  ///havoqgt::havoqgt_finalize();

  return 0;  
} // main

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

#ifndef HAVOQGT_MAILBOX_HPP_INCLUDED
#define HAVOQGT_MAILBOX_HPP_INCLUDED


#include <havoqgt/mpi.hpp>
#include <havoqgt/environment.hpp>

#include <vector>
#include <deque>

#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>


namespace havoqgt {


template <typename T>
class mailbox {
  const char* msm_fname = "havoqgt_mailbox_msm";
  template<class OPT> using offset_ptr = boost::interprocess::offset_ptr<OPT>;
  using managed_shared_memory = boost::interprocess::managed_shared_memory;

  struct msg_wrapper; 
  struct msg_bundle_shm;
  struct msg_bundle_mpi;
  struct shm_exchange;
public:
  mailbox(int _mpi_tag) {
    m_mpi_tag = _mpi_tag;
    m_mpi_comm = havoqgt_env()->node_offset_comm().comm();
    m_mpi_rank = havoqgt_env()->node_offset_comm().rank();
    m_mpi_size = havoqgt_env()->node_offset_comm().size();
    m_shm_rank = havoqgt_env()->node_local_comm().rank();
    m_shm_size = havoqgt_env()->node_local_comm().size();
    m_world_rank = havoqgt_env()->world_comm().rank();
  
    init_environment_config();

    init_managed_shared_memory();

    init_alloc_bundle_shared();

    init_alloc_bundle_mpi();

    if(m_world_rank == 0) {
      std::cout << "Number of nodes = " << m_mpi_size << std::endl;
      std::cout << "Ranks per node = " << m_shm_size << std::endl;
      std::cout << "Sizeof(shm_exchange) = " << sizeof(shm_exchange) << std::endl;
      std::cout << "Sizeof(msg_wrapper) = " << sizeof(msg_wrapper) << std::endl;
    }
    //lock_time = 0;
    m_send_recv_balance = 0;
    m_isend_count = 0;
    m_isend_bytes = 0;
  }

  ~mailbox() {
    if(m_world_rank == 0) {
    std::cout << "ISend Count = " << m_isend_count << std::endl;
    std::cout << "ISend Bytes = " << m_isend_bytes << std::endl;
    double ave = (m_isend_count) ? (double(m_isend_bytes) / double(m_isend_count)) : double(0);
    std::cout << "Ave size = " << ave << std::endl;
    std::cout << "m_shm_transfer_count = " << m_shm_transfer_count << std::endl;
    }
    //if (m_world_rank == 0)
    //  std::cout << "Lock Time = " << lock_time << std::endl;

    /*lots to free here.....

      Should we use try_send() -- look at old OpenMP code
     */

    m_my_exchange->~shm_exchange(); //frees mutexes
    delete m_pmsm;        //frees managed_shared_memory
    havoqgt_env()->node_local_comm().barrier();
    boost::interprocess::shared_memory_object::remove(msm_fname); //removes shared memory file
    for(auto itr = m_free_mpi_list.begin(); itr != m_free_mpi_list.end(); ++itr) {
      free(*itr);

    }
    havoqgt_env()->world_comm().barrier();
  }

private:
  void init_environment_config() {
    if(s_num_isend == 0) {
      s_num_isend = havoqgt_getenv("HAVOQGT_MAILBOX_NUM_ISEND", uint32_t(4));
      s_num_irecv = havoqgt_getenv("HAVOQGT_MAILBOX_NUM_IRECV", uint32_t(4));
      uint32_t mpi_bytes = havoqgt_getenv("HAVOQGT_MAILBOX_MPI_SIZE", uint32_t(4096));
      msg_bundle_mpi::set_capacity_bytes(mpi_bytes);
      uint32_t shm_bytes = havoqgt_getenv("HAVOQGT_MAILBOX_SHM_SIZE", uint32_t(4096));
      msg_bundle_shm::set_capacity_bytes(shm_bytes);
      s_b_print = havoqgt_getenv("HAVOQGT_MAILBOX_PRINT_STATS", bool(true));
      s_b_route_on_dest = havoqgt_getenv("HAVOQGT_MAILBOX_ROUTE_ON_DEST", bool(false));
      assert(s_num_isend > 0);

      //Quick print
      if(m_world_rank == 0) {
        std::cout << "HAVOQGT_MAILBOX_NUM_ISEND     = " << s_num_isend << std::endl;
        std::cout << "HAVOQGT_MAILBOX_NUM_IRECV     = " << s_num_irecv << std::endl;
        std::cout << "HAVOQGT_MAILBOX_MPI_SIZE      = " << mpi_bytes   << std::endl;
        std::cout << "HAVOQGT_MAILBOX_SHM_SIZE      = " << shm_bytes   << std::endl;
        std::cout << "HAVOQGT_MAILBOX_PRINT_STATS   = " << s_b_print << std::endl; 
        std::cout << "HAVOQGT_MAILBOX_ROUTE_ON_DEST = " << s_b_route_on_dest << std::endl;

        std::cout << "msg_bundle_mpi::capacity = " << msg_bundle_mpi::capacity << std::endl;
        std::cout << "msg_bundle_shm::capacity = " << msg_bundle_shm::capacity << std::endl;
      }
    }
  }

  void init_managed_shared_memory() {
    //
    //Open Shared Mem segment
    size_t shm_file_size = msg_bundle_shm::padded_size * shm_exchange::capacity * m_shm_size * 2;

    m_shm_rank = havoqgt_env()->node_local_comm().rank();
    if(m_shm_rank == 0) {
      boost::interprocess::shared_memory_object::remove(msm_fname);
      m_pmsm = new managed_shared_memory( boost::interprocess::create_only, msm_fname, shm_file_size);
      m_poffset_exchange = m_pmsm->construct<offset_ptr<shm_exchange>>("exchange")[m_shm_size]();
    }
    havoqgt_env()->node_local_comm().barrier();
    if(m_shm_rank != 0) {
      m_pmsm = new managed_shared_memory( boost::interprocess::open_only, msm_fname );
      std::pair<offset_ptr<shm_exchange>*, managed_shared_memory::size_type> res;
      res = m_pmsm->find<offset_ptr<shm_exchange>>("exchange");
      assert(res.second == havoqgt_env()->node_local_comm().size());
      m_poffset_exchange = res.first;
    }
    havoqgt_env()->node_local_comm().barrier();
    void* aligned = m_pmsm->allocate_aligned(sizeof(shm_exchange), 4096);
    m_my_exchange = new (aligned) shm_exchange(m_shm_rank);
    m_poffset_exchange[m_shm_rank] = m_my_exchange;
    m_shm_transfer_count=0;
    havoqgt_env()->node_local_comm().barrier();
    m_pp_exchange.resize(m_shm_size, nullptr);
    for(size_t i=0; i<m_shm_size; ++i) {
      m_pp_exchange[i] = m_poffset_exchange[i].get();
    }
    havoqgt_env()->node_local_comm().barrier();
  }

  void init_alloc_bundle_shared() {
    //
    // Allocate large chunk to help NUMA page pacement.  
    // Local rank always touches pages before sending to other ranks.
    char* chunk = (char*) m_pmsm->allocate_aligned( msg_bundle_shm::padded_size * (shm_exchange::capacity), 4096);
    for(size_t i=0; i<(shm_exchange::capacity); ++i) {
      void* addr = chunk + i*msg_bundle_shm::padded_size;
      msg_bundle_shm* ptr = new (addr) msg_bundle_shm(m_shm_rank);
      assert(ptr->size == 0);
      assert(ptr->source_core == m_shm_rank );
      m_my_exchange->free(ptr);
    }
    m_my_exchange->count_returned = 0;
    //
    // Allocate per_rank data
    m_bundle_per_shm_rank.resize(m_shm_size, nullptr);
    m_pending_iterator_per_shm_rank.resize(m_shm_size, m_shm_pending_list.end());
  }

  void init_alloc_bundle_mpi() {
    size_t num_to_alloc = s_num_isend + s_num_irecv + m_mpi_size;
    m_free_mpi_list.reserve( num_to_alloc * 2);
    for(size_t i=0; i<num_to_alloc; ++i) {
      void* addr = nullptr;
      int ret = posix_memalign(&addr, 4096, msg_bundle_mpi::padded_size);
      if(ret != 0) {
        HAVOQGT_ERROR_MSG("posix_memalign failed");
      }
      m_free_mpi_list.push_back( new (addr) msg_bundle_mpi() );
    }
    //
    // Allocate per rank data
    m_bundle_per_mpi_rank.resize(m_mpi_size, nullptr);
    m_pending_iterator_per_mpi_rank.resize(m_mpi_size, m_mpi_pending_list.end());

    for(size_t i=0; i<s_num_irecv; ++i) {
      post_new_irecv(get_free_mpi_bundle());
    }
  }

  msg_bundle_mpi* get_free_mpi_bundle() {
    msg_bundle_mpi* to_return;
    if(m_free_mpi_list.empty()) {
      void* addr = nullptr;
      int ret = posix_memalign(&addr, 4096, msg_bundle_mpi::padded_size);
      if(ret != 0) {
        HAVOQGT_ERROR_MSG("posix_memalign failed");
      }
      to_return = new (addr) msg_bundle_mpi();
    } else {
      to_return = m_free_mpi_list.back();
      m_free_mpi_list.pop_back();
    }
    to_return->size = 0;
    return to_return;
  }

  void free_mpi_bundle(msg_bundle_mpi* tofree) {
    tofree->size = 0;
    m_free_mpi_list.push_back(tofree);
  }

public:

  template <typename OutputIterator>
  void send(size_t world_dest, const T& raw_msg, OutputIterator oitr) {
    ++m_send_recv_balance;
    if(world_dest == m_world_rank) {
      *oitr = raw_msg;
      ++oitr;
    }
  
    //assume block partitioning
    msg_wrapper wrapped;
    wrapped.dest_node = world_dest / m_shm_size;
    wrapped.dest_core = world_dest % m_shm_size;
    wrapped.bcast = false;
    wrapped.intercept = false;
    wrapped.msg = raw_msg;

    bool b_sent = false;

    if(s_b_route_on_dest)  {
      if(wrapped.dest_node == m_mpi_rank) {
        assert(wrapped.dest_core != m_shm_rank);
        b_sent = route_shm(wrapped.dest_core, wrapped, oitr);
      } else {
        size_t first_core_route = wrapped.dest_node % m_shm_size;
        if(first_core_route != m_shm_rank) {
          b_sent = route_shm(first_core_route, wrapped, oitr);
        } else {
          b_sent = route_mpi(wrapped.dest_node, wrapped, oitr);
        }
      }
    } else {
      if(wrapped.dest_core != m_shm_rank) {
        b_sent = route_shm(wrapped.dest_core, wrapped, oitr);
      } else if (wrapped.dest_node != m_mpi_rank) {
        b_sent = route_mpi(wrapped.dest_node, wrapped, oitr);
      } else {
        HAVOQGT_ERROR_MSG("Logic Error");
      }
    }

    if(b_sent) {
      receive(oitr);
      bool print = false;
      while(shm_transfer_slots_full() || isend_slots_full()) {
        receive(oitr);
        if(print) {
          print = false;
          std::cout << havoqgt_env()->whoami() << " is stuck in send" << std::endl;
        }
      }
    }
  }

  template <typename OutputIterator>
  void receive(OutputIterator oitr) {
    receive_mpi(oitr);
    receive_shm(oitr);
  }

  bool is_idle() {
    cleanup_pending_isend_requests();
    return m_mpi_pending_list.empty() && m_isend_request_list.empty() && m_shm_pending_list.empty() 
           && !m_my_exchange->probe() && shm_count_transfers_pending() == 0;
  }

  void flush_buffers_if_idle() {
    static double last_time = 0;
    double new_time = MPI_Wtime();
    if(new_time - last_time < 1e-6) return;
    static bool flopper = false;
    cleanup_pending_isend_requests();
    if(m_isend_request_list.empty() && shm_count_transfers_pending() == 0 && !m_my_exchange->probe()) {
      flopper = !flopper;
      if(flopper) {
        if(!m_shm_pending_list.empty()) {
          try_transfer_shm(m_shm_pending_list.front());
          last_time = new_time;
        }
      } else {
        if(!m_mpi_pending_list.empty()) {
          post_isend(m_mpi_pending_list.front());
          last_time = new_time;
        }
      }
    }
/*

    if(m_isend_request_list.empty() && !m_shm_pending_list.empty() && shm_count_transfers_pending() == 0 && !m_my_exchange->probe()) {
      bool sent = try_transfer_shm(m_shm_pending_list.front());
      //if(sent) return;
    }

    cleanup_pending_isend_requests();

    if(m_isend_request_list.empty() && !m_mpi_pending_list.empty()) {
      if(!m_mpi_pending_list.empty()) {
        post_isend(m_mpi_pending_list.front());
      }
    }*/
  }

  uint64_t shm_count_transfers_pending() { return m_shm_transfer_count - m_my_exchange->get_count_returned(); }

private:

  template <typename OutputIterator>
  bool route_shm(size_t shm_rank, msg_wrapper& wrapped, OutputIterator oitr) {
    bool to_return = false;
    if(m_bundle_per_shm_rank[shm_rank] == nullptr) {
      bool print = true;
      while(!m_my_exchange->has_free()) {
        receive_shm(oitr);    // While shm is waiting on free buffer, receive mpi
        receive_mpi(oitr);
        if(print) {
          print = false;
          std::cout <<  havoqgt_env()->whoami() << " Please don't be here..." << std::endl;
        }
      }
      msg_bundle_shm* bundle = m_my_exchange->get_free();

      //std::cout << havoqgt_env()->whoami() << " route_shm size = " << bundle->size << ", source_core = " << bundle->get_source_core() << std::endl;
      m_bundle_per_shm_rank[shm_rank] = bundle;
      assert(m_bundle_per_shm_rank[shm_rank]->size == 0);
      assert(m_bundle_per_shm_rank[shm_rank]->source_core == m_shm_rank);
      assert(m_pending_iterator_per_shm_rank[shm_rank] == m_shm_pending_list.end());
      m_shm_pending_list.push_back(shm_rank);
      m_pending_iterator_per_shm_rank[shm_rank] = --(m_shm_pending_list.end());
    }
    size_t size = m_bundle_per_shm_rank[shm_rank]->push_back(wrapped);
    
    if(size == msg_bundle_shm::capacity) {
      to_return = true;
      /*if(!fast_path && shm_rank != m_shm_pending_list.front()) {
         while(!try_transfer_shm( m_shm_pending_list.front())) {
          if(fast_path) {
            HAVOQGT_ERROR_MSG("route_shm try_transfer_shm failed");
          } else {
            std::cout << "Add me up 1" << std::endl;
            receive_shm(oitr);  
            receive_mpi(oitr);
          }
        }
      }*/
      bool print = false;
      while(!try_transfer_shm(shm_rank)) {
        if(print) {
          print = true;
          std::cout << havoqgt_env()->whoami() << " is stuck in route_shm, try_transfer_shm" << std::endl;
        }
        receive_shm(oitr);  
        receive_mpi(oitr);
      }
    }
    return to_return;
  }
  bool shm_transfer_slots_full() {
    uint64_t pending = shm_count_transfers_pending();
    return pending > s_num_isend;
  }

  bool isend_slots_full() {
    cleanup_pending_isend_requests(); 
    return m_isend_request_list.size() > s_num_isend;
  }

  template <typename OutputIterator>
  bool route_mpi(size_t mpi_rank, msg_wrapper& wrapped, OutputIterator oitr) {
    bool to_return = false;
    if(m_bundle_per_mpi_rank[mpi_rank] == nullptr) {
      m_bundle_per_mpi_rank[mpi_rank] = get_free_mpi_bundle();
      assert(m_bundle_per_mpi_rank[mpi_rank]->size == 0);
      assert(m_pending_iterator_per_mpi_rank[mpi_rank] == m_mpi_pending_list.end());
      m_mpi_pending_list.push_back(mpi_rank);
      m_pending_iterator_per_mpi_rank[mpi_rank] = --(m_mpi_pending_list.end());
    }
    size_t size = m_bundle_per_mpi_rank[mpi_rank]->push_back(wrapped);

    if(size == msg_bundle_mpi::capacity) {
      to_return = true;
      /*if(!fast_path && mpi_rank != m_mpi_pending_list.front()) {
        post_isend(m_mpi_pending_list.front()); //fair sending
      }*/
      post_isend(mpi_rank);
    }
    return to_return;
  }

  bool try_transfer_shm(size_t rank) {
    assert(*(m_pending_iterator_per_shm_rank[rank]) == rank);
    msg_bundle_shm* to_transfer = m_bundle_per_shm_rank[rank];
    if(m_pp_exchange[rank]->try_send(to_transfer)) {
      m_bundle_per_shm_rank[rank] = nullptr;
      m_shm_pending_list.erase(m_pending_iterator_per_shm_rank[rank]);
      m_pending_iterator_per_shm_rank[rank] = m_shm_pending_list.end();
      m_shm_transfer_count++;
      return true;
    }
    //std::cout << havoqgt_env()->whoami() << " transfer_shm FAILED dest_core = " << rank << std::endl;
    return false;
  }

  void post_isend(size_t rank) {
    assert(*(m_pending_iterator_per_mpi_rank[rank]) == rank);
    assert(m_pending_iterator_per_mpi_rank[rank] != m_mpi_pending_list.end()); 
    msg_bundle_mpi* to_transfer = m_bundle_per_mpi_rank[rank];
    m_bundle_per_mpi_rank[rank] = nullptr;
    std::pair<MPI_Request, msg_bundle_mpi*> req_pair;
    MPI_Request* request_ptr=&req_pair.first;
    req_pair.second = to_transfer;
    void* buffer_ptr = (void*) to_transfer;
    size_t size_in_bytes = to_transfer->message_size();
    //std::cout << havoqgt_env()->whoami() << " posting ISend to node " << rank << std::endl;
    //std::cout << "Inspecting contents, byte_size = " << size_in_bytes << " size = " << to_transfer->size << ", first hop_count = " << to_transfer->data[0].msg.m_hop_count << std::endl;
    CHK_MPI( MPI_Isend(buffer_ptr, size_in_bytes, MPI_BYTE, rank, m_mpi_tag, m_mpi_comm, request_ptr) );

    m_isend_count++;
    m_isend_bytes+=size_in_bytes;

    m_isend_request_list.push_back(req_pair);
    m_mpi_pending_list.erase(m_pending_iterator_per_mpi_rank[rank]);
    m_pending_iterator_per_mpi_rank[rank] = m_mpi_pending_list.end();
    //std::cout << havoqgt_env()->whoami() << " Inspecting m_mpi_pending_list:  size = " << m_mpi_pending_list.size()  << std::endl;
    /*for(auto itr = m_mpi_pending_list.begin(); itr != m_mpi_pending_list.end(); ++itr) {
      //std::cout << *itr << std::endl;
    }*/
    //std::cout << "Inspecting m_isend_request_list: size = " << m_isend_request_list.size() << std::endl;
  }

  template <typename OutputIterator> 
  void receive_mpi(OutputIterator oitr) {
    m_send_recv_balance = 0;
    int flag(0);
    do {
    MPI_Request* request_ptr = &(m_irecv_request_list.front().first);
    CHK_MPI( MPI_Test( request_ptr, &flag, MPI_STATUS_IGNORE) );
    if(flag) {
      msg_bundle_mpi* ptr = m_irecv_request_list.front().second;
      for(size_t i=0; i<ptr->size; ++i) {
        if(s_b_route_on_dest) {
          assert(ptr->data[i].dest_node == m_mpi_rank);
          if(ptr->data[i].dest_core == m_shm_rank) {
            *oitr = ptr->data[i].msg; 
            ++oitr;
          } else {
            route_shm(ptr->data[i].dest_core, ptr->data[i], oitr);
          }
        } else {
          assert(ptr->data[i].dest_core == m_shm_rank);
          assert(ptr->data[i].dest_node == m_mpi_rank);
          *oitr = ptr->data[i].msg;
          ++oitr;
        }
      }
      m_irecv_request_list.pop_front();
      ptr->size = 0;
      post_new_irecv(ptr);
    }
    } while(flag);
  }

  template <typename OutputIterator> 
  void receive_shm(OutputIterator oitr) {
    m_send_recv_balance = 0;
    std::vector<msg_bundle_shm*> to_recv;
    m_my_exchange->try_recv(to_recv);
    for(auto itr = to_recv.begin(); itr != to_recv.end(); ++itr) {


#if 0
      msg_bundle_shm* recvptr = *itr;
      std::vector<msg_wrapper> copy(recvptr->data, recvptr->data+recvptr->size);
      m_pp_exchange[recvptr->source_core]->free(recvptr);


    //if(recvptr != nullptr) {
      for(int i=0; i</*recvptr->size*/copy.size(); ++i) {
        if(s_b_route_on_dest) {
          if(/*recvptr->data*/copy[i].dest_node == m_mpi_rank && /*recvptr->data*/copy[i].dest_core == m_shm_rank) {
            *oitr = /*recvptr->data*/copy[i].msg; 
            ++oitr; 
          } else {
            if(copy[i].dest_node == m_mpi_rank) {
              std::cout << "logic problem!!!" << std::endl;
            }
            route_mpi(/*recvptr->data*/copy[i].dest_node, /*recvptr->data*/copy[i], oitr);
          }
        } else {
          assert(/*recvptr->data*/copy[i].dest_core == m_shm_rank);
          if(/*recvptr->data*/copy[i].dest_node == m_mpi_rank) {
            *oitr = /*recvptr->data*/copy[i].msg;
            ++oitr;
          } else {
            route_mpi(/*recvptr->data*/copy[i].dest_node, /*recvptr->data*/copy[i], oitr);
          }
        }
      }
      //m_pp_exchange[recvptr->source_core]->free(recvptr);
    //}

#else
      msg_bundle_shm* recvptr = *itr;
    if(recvptr != nullptr) {
      for(int i=0; i<recvptr->size; ++i) {
        if(s_b_route_on_dest) {
          if(recvptr->data[i].dest_node == m_mpi_rank && recvptr->data[i].dest_core == m_shm_rank) {
            *oitr = recvptr->data[i].msg; 
            ++oitr; 
          } else {
            route_mpi(recvptr->data[i].dest_node, recvptr->data[i], oitr);
          }
        } else {
          assert(recvptr->data[i].dest_core == m_shm_rank);
          if(recvptr->data[i].dest_node == m_mpi_rank) {
            *oitr = recvptr->data[i].msg;
            ++oitr;
          } else {
            route_mpi(recvptr->data[i].dest_node, recvptr->data[i], oitr);
          }
        }
      }
      m_pp_exchange[recvptr->source_core]->free(recvptr);
    }
 #endif
    }
  }



  void cleanup_pending_isend_requests() {
    while(!m_isend_request_list.empty()) {
      int flag(0);
      MPI_Request* request_ptr = &(m_isend_request_list.front().first);
      CHK_MPI( MPI_Test( request_ptr, &flag, MPI_STATUS_IGNORE) );
      if(flag) {
        free_mpi_bundle(m_isend_request_list.front().second);
        m_isend_request_list.pop_front();
      } else {
        break;
      }
    }
  }

  void post_new_irecv(msg_bundle_mpi* buff) {
    std::pair<MPI_Request, msg_bundle_mpi*> irecv_req;
    irecv_req.second = buff;
    MPI_Request* request_ptr = &irecv_req.first;
    CHK_MPI( MPI_Irecv( (void*) buff, msg_bundle_mpi::padded_size, MPI_BYTE, MPI_ANY_SOURCE, m_mpi_tag, m_mpi_comm, request_ptr) );
    m_irecv_request_list.push_back(irecv_req);
  }


private:

  int m_world_rank;

  //
  // MPI Related
  int m_mpi_tag;
  MPI_Comm m_mpi_comm;
  int m_mpi_size;
  int m_mpi_rank;
  std::vector<msg_bundle_mpi*>                        m_bundle_per_mpi_rank;
  std::vector<msg_bundle_mpi*>                        m_free_mpi_list;
  std::vector< std::list<size_t>::iterator >          m_pending_iterator_per_mpi_rank;
  std::list<size_t>                                   m_mpi_pending_list;
  std::deque< std::pair<MPI_Request, msg_bundle_mpi*>> m_irecv_request_list;
  std::deque< std::pair<MPI_Request, msg_bundle_mpi*>> m_isend_request_list;


  // 
  // Shared Mem Related
  managed_shared_memory*    m_pmsm;
  offset_ptr<shm_exchange>* m_poffset_exchange;
  std::vector<shm_exchange*> m_pp_exchange;
  shm_exchange*             m_my_exchange;
  int                       m_shm_rank;
  int                       m_shm_size;
  std::vector<msg_bundle_shm*>                        m_bundle_per_shm_rank;
  std::vector< std::list<size_t>::iterator >          m_pending_iterator_per_shm_rank;
  std::list<size_t>                                   m_shm_pending_list;
  uint64_t                  m_shm_transfer_count;

  //
  // Satic Configs
  static uint32_t s_num_isend;
  static uint32_t s_num_irecv;
  static bool     s_b_print;
  static bool s_b_route_on_dest;

  //
  // 
  uint64_t m_send_recv_balance;
  uint64_t m_isend_count;
  uint64_t m_isend_bytes;


}; 

template <typename T>
uint32_t mailbox<T>::s_num_isend = 0;
template <typename T>
uint32_t mailbox<T>::s_num_irecv = 0;
template <typename T>
bool     mailbox<T>::s_b_print = 0;
template <typename T>
bool     mailbox<T>::s_b_route_on_dest = 0;

template<typename T>
struct mailbox<T>::shm_exchange {
  using mutex = boost::interprocess::interprocess_mutex;
  using scoped_lock = boost::interprocess::scoped_lock<mutex>;
  static const uint32_t capacity = 2400;
  volatile uint64_t count_returned;
  volatile uint64_t size_free;
  char pad1[64-sizeof(count_returned)-sizeof(size_free)];
  volatile uint64_t recv_end;
  char pad2[65-sizeof(recv_end)];
  volatile uint64_t recv_beg;
  uint64_t source_core;
  mutex free_mutex;
  mutex recv_mutex;
  offset_ptr<msg_bundle_shm> free_list[capacity];
  offset_ptr<msg_bundle_shm> recv_list[capacity];
  shm_exchange(uint32_t core) { 
    size_free = 0;
    recv_end = 0;
    recv_beg = 0;
    source_core = core;
    count_returned = 0;
  }
  void free(msg_bundle_shm* to_free) {
    //if(size_free >= capacity-5) {
    //  //std::cout << "size_free >= capacity -- Rank = " << havoqgt_env()->node_local_comm().rank() << ", source_core == " << source_core << ", to_free->source_core = " << to_free->get_source_core() << " size_free = " << size_free << std::endl;
    //}
    to_free->size = 0;
    //double lock_start = MPI_Wtime();
    scoped_lock lock(free_mutex, boost::interprocess::try_to_lock);
    while(!lock) lock.try_lock();
    //lock_time += MPI_Wtime() - lock_start;
    ++count_returned;
    assert(size_free < capacity);
    free_list[size_free++] = to_free;
  }

  uint64_t get_count_returned() {
    //scoped_lock lock(free_mutex);
    __sync_synchronize();
    return count_returned;
  }

  msg_bundle_shm* get_free() {
    //std::cout << havoqgt_env()->whoami() << " get_free() size_free = " << size_free;
    //double lock_start = MPI_Wtime();
    scoped_lock lock(free_mutex, boost::interprocess::try_to_lock);
    while(!lock) lock.try_lock();
    //lock_time += MPI_Wtime() - lock_start;
    assert(size_free > 0);
    if(size_free == 0) {
      HAVOQGT_ERROR_MSG("BAD SHIT HAPPENED");
    }
    msg_bundle_shm* to_return = free_list[--size_free].get();
    //std::cout << " source_core = " << free_list[size_free]->get_source_core() << ", size = " << free_list[size_free]->size << std::endl;
    assert(to_return->size == 0);
    return to_return;
  }

  bool has_free() {
    //scoped_lock lock(free_mutex);
    __sync_synchronize();
    bool to_return = size_free > 0;
    __sync_synchronize();
    return to_return;
  }

  bool try_send(msg_bundle_shm* to_send) {
    assert(to_send->get_source_core() == havoqgt_env()->node_local_comm().rank());
    //std::cout << havoqgt_env()->whoami() << " sending to " << source_core << ", size = " << to_send->size << std::endl;
    //double lock_start = MPI_Wtime();
    scoped_lock lock(recv_mutex, boost::interprocess::try_to_lock);
    bool print = false;
    while(!lock) {
      if(print) {
        print = false;
        std::cout << havoqgt_env()->whoami() << " lock failed" << std::endl;
      }
      lock.try_lock();
      //sched_yield();
    }
    //lock_time += MPI_Wtime() - lock_start;
    if(/*size_recv < capacity*/ /*recv_end >= recv_beg &&*/ recv_end - recv_beg < capacity) {
      recv_list[recv_end++ % capacity] = to_send;
      return true;
    } 
    //std::cout << havoqgt_env()->whoami() << ": Try_send Failed, sending_to_core = " << source_core << ", recv_end = " << recv_end << ", recv_beg = " << recv_beg << std::endl;
    return false;
  }

  bool probe() {
    __sync_synchronize();
    return recv_end > recv_beg;
  }

  void try_recv(std::vector<msg_bundle_shm*>& to_recv) {
    //double lock_start = MPI_Wtime();
    to_recv.clear();
    __sync_synchronize();
    if(recv_end == recv_beg) return;
    //to_recv.reserve(recv_end - recv_beg);
    scoped_lock lock(recv_mutex, boost::interprocess::try_to_lock);
    while(!lock) {
      lock.try_lock();
    }
    //lock_time += MPI_Wtime() - lock_start;
    while(/*size_recv > 0*/recv_end > recv_beg) {
      /*//std::cout << havoqgt_env()->whoami() << " recv size = " << size_recv << std::endl;
      to_recv.resize(size_recv);
      for(size_t i=0; i<size_recv; ++i) {
        to_recv[i] = recv_list[i].get();
        //std::cout << "recving from: " << recv_list[i]->get_source_core() << ", size = " << recv_list[i]->size <<", size = " << recv_list[i]->size <<  std::endl;
        //std::cout << "recving from: " << to_recv[i]->get_source_core() << ", size = " << to_recv[i]->size <<", size = " << to_recv[i]->size <<  std::endl;
      }
      size_recv = 0;
      */
      //return recv_list[/*--size_recv*/recv_beg++ % capacity].get();
      to_recv.push_back(recv_list[recv_beg++ % capacity].get());
    }
    //return nullptr;
  }
};


template<typename T>
struct mailbox<T>::msg_wrapper {
  uint64_t dest_node : 16;
  uint64_t dest_core : 8;
  uint64_t bcast     : 1;
  uint64_t intercept : 1;
  T        msg;
};

template<typename T>
struct mailbox<T>::msg_bundle_shm {
  // Data Members
  uint8_t  source_core;
  uint32_t size;
  msg_wrapper data[0];
  msg_bundle_shm(size_t core)
    : source_core(core)
    , size(0)
  {
    assert(core == source_core);
    assert(source_core == havoqgt_env()->node_local_comm().rank());
  }
  // Static Members & Functions
  static uint32_t capacity;
  static uint32_t padded_size;
  static void set_capacity_bytes(uint32_t bytes) { 
    msg_bundle_shm::padded_size = bytes;
    msg_bundle_shm::capacity = (padded_size - sizeof(msg_bundle_mpi)) / sizeof(msg_wrapper);
  } 
  size_t push_back(const msg_wrapper& _d) {
    data[size] = _d;
    return ++size;
  }

  size_t get_source_core() {
    return source_core;
  }

};

template <typename T>
uint32_t mailbox<T>::msg_bundle_shm::capacity = 0;

template <typename T>
uint32_t mailbox<T>::msg_bundle_shm::padded_size = 0;

template<typename T>
struct mailbox<T>::msg_bundle_mpi {
  uint32_t size;
  msg_wrapper data[0];
  msg_bundle_mpi() 
    : size(0) 
  { } 
  // Static Members & Functions
  static uint32_t capacity;
  static uint32_t padded_size;
  static void set_capacity_bytes(uint32_t bytes) { 
    msg_bundle_mpi::padded_size = bytes;
    msg_bundle_mpi::capacity = (padded_size - sizeof(msg_bundle_shm)) / sizeof(msg_wrapper); //matches msg_bundle_shm's
  }
  size_t message_size() { return sizeof(msg_wrapper[size]) + sizeof(msg_bundle_mpi); }
  size_t push_back(const msg_wrapper& _d) {
    data[size] = _d;
    return ++size;
  }
};

template <typename T>
uint32_t mailbox<T>::msg_bundle_mpi::capacity = 0;

template <typename T>
uint32_t mailbox<T>::msg_bundle_mpi::padded_size = 0;

} //namespace havoqgt

#endif //HAVOQGT_MAILBOX_HPP_INCLUDED

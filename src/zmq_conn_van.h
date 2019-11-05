/**
 *  Copyright (c) 2015 by Contributors
 */
#ifndef PS_FABRIC_VAN2_H_
#define PS_FABRIC_VAN2_H_
#include <stdio.h>
#include <cstdlib>
#include <zmq.h>
#include <string>
#include <cstring>
#include <thread>
#include <cmath>
#include <atomic>
#include <tuple>
#include "ps/internal/threadsafe_queue.h"
#include "ps/internal/van.h"
#include "zmq_van.h"
#if _MSC_VER
#define rand_r(x) rand()
#endif


#ifdef __GNUC__
#define DMLC_PS_OFI_LIKELY(x)  __builtin_expect((x), 1)
#define DMLC_PS_OFI_UNLIKELY(x)  __builtin_expect((x), 0)
#else
#define DMLC_PS_OFI_LIKELY(x)  (x)
#define DMLC_PS_OFI_UNLIKELY(x)  (x)
#endif

#define DMLC_PS_OFI_MAJOR_VERSION  (1)
#define DMLC_PS_OFI_MINOR_VERSION  (6)
#define dmlc_ps_ofi_version    FI_VERSION(DMLC_PS_OFI_MAJOR_VERSION, \
                                          DMLC_PS_OFI_MINOR_VERSION)
#define DMLC_PS_MAX_PROV_INFO    (15)

// We have a limit of MAX_HANDLE_SIZE = 64 bytes. Therefore, we can only
// support an endpoint name of maximum 56 bytes. We are using remaining
// 8 bytes for tags.
#define DMLC_PS_MAX_EP_ADDR (56)

// We are supporting minimum 2^32 rings per endpoint and reserving 1 bit
// for marking control sends/recvs.
#define MIN_TAG_BITS_FOR_RING_ID  (32 + 1)

// For each tag, we use MSB as control bit and remaining
// for identifying different rings. We look at mem_tag_format for
// an endpoint to determine if provider is reserving any MSBs.
#define OFI_HIGHEST_TAG_BIT    (0x1UL << 63)


#define check_err(ret, msg) do {                          \
        if (DMLC_PS_OFI_UNLIKELY(ret != 0)) {             \
          LOG(FATAL) << msg << ". RC: " << ret            \
                     << ". ERROR: " << fi_strerror(-ret); \
        }                                                 \
} while (false)

#define EFA_PROVIDER_NAME "efa"
#define IS_EFA_PROVIDER(NAME) (strcmp((NAME), EFA_PROVIDER_NAME)==0)

/* This is twice the size of maximum inflight requests supported by NCCL */
#define DMLC_PS_OFI_MAX_REQUESTS  256


#include <rdma/fi_errno.h>
#include <rdma/fabric.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>

namespace ps {

struct ZmqBufferContext2 { // for clarity, don't merge meta and data
  int sender;
  zmq_msg_t* meta_zmsg;
  std::vector<zmq_msg_t*> data_zmsg;
};

/**
 * \brief be smart on freeing recved data
 */
inline void FreeData2(void* data, void* hint) {
  if (hint == NULL) {
    delete[] static_cast<char*>(data);
  } else {
    delete static_cast<SArray<char>*>(hint);
  }
}

/**
 * \brief ZMQ based implementation
 */
class FabricVan2 : public Van {

struct Stack {
  int *array;
  int top;
  int size;
};

struct FreeList {
  // Array of free buffers
  void *buffers;

  // Stack of free buffer indexes
  Stack *free_index;

  // Size of buffers array
  uint64_t size;
};

struct ListenComm {
  uint64_t tag;
  struct fid_ep *local_ep;
  //int dev;
  bool accepted;
};

struct SendComm {
  uint64_t tag;
  uint64_t num_inflight_reqs;
  fi_addr_t remote_ep;
  struct fid_ep *local_ep;
  FreeList *reqs_fl;
  FreeList *pending_reqs_fl;
};

struct RecvComm {
  uint64_t tag;
  uint64_t num_inflight_reqs;
  fi_addr_t remote_ep;
  struct fid_ep *local_ep;
  FreeList *reqs_fl;
};

enum RequestState {
  kRequestCreated = 0,
  kRequestPending,
  kRequestCompleted,
  kRequestError,
};

enum RequestDirection {
  kRequestSend = 1,
  kRequestRecv,
};

struct Request {
  // Associated Comm object
  union {
    ListenComm *l_comm;
    SendComm *s_comm;
    RecvComm *r_comm;
  };

  // Buffer index
  uint64_t buffer_index;

  // Associated OFI Context
  struct fi_context ctx;

  // Associated Device ID
  //int dev;

  // Size of completed request
  size_t size;

  // State of request
  RequestState state;

  // Direction of request
  RequestDirection direction;
};


struct PendingReq {
  // Associated request
  Request* ofi_req;

  // Send/Recv Metadata
  void *data;
  size_t len;
  int type;
};

struct PendingReqElem {
  PendingReqElem* next;

  // Buffer index
  uint64_t buffer_index;

  // Pending request to retry
  PendingReq pending_req;
};


struct PendingReqQueue {
  PendingReqElem *head;
  PendingReqElem *tail;
};


// TDDO call it an endpoint?
struct OfiEndpoint {
  // Current available tag ID.
  // XXX In case multiple NICs are available
  uint64_t tag;

  // Maximum supported tag ID
  uint64_t max_tag;

  // Count of CQEs to read from CQ
  uint64_t num_cqes;

  // Provider name
  char *prov_name;

  // Fabric handle
  struct fid_fabric *fabric;

  // Access Domain handle
  struct fid_domain *domain;

  // Endpoint handle to communicate to
  struct fid_ep *ep;

  // Address vector handle
  struct fid_av *av;

  // Completion Queue handle
  struct fid_cq *cq;

  // Pending requests queue
  PendingReqQueue *pending_req_q;
};

 public:
  FabricVan2() {}
  virtual ~FabricVan2() {}

 protected:
  void Start(int customer_id) override {
    // start zmq
    start_mu_.lock();
    OfiInit();
    start_mu_.unlock();
    customer_id_ = customer_id;
    ZmqStart();
    Van::Start(customer_id);
  }

  void ZmqStart() {
    start_mu_.lock();
    if (context_ == nullptr) {
      context_ = zmq_ctx_new();
      CHECK(context_ != NULL) << "create 0mq context failed";
    }
    start_mu_.unlock();

    auto val1 = Environment::Get()->find("BYTEPS_ZMQ_MAX_SOCKET");
    int byteps_zmq_max_socket = val1 ? atoi(val1) : 1024;
    zmq_ctx_set(context_, ZMQ_MAX_SOCKETS, byteps_zmq_max_socket);
    PS_VLOG(1) << "BYTEPS_ZMQ_MAX_SOCKET set to " << byteps_zmq_max_socket;

    auto val2 = Environment::Get()->find("BYTEPS_ZMQ_NTHREADS");
    int byteps_zmq_nthreads = val2 ? atoi(val2) : 4;
    zmq_ctx_set(context_, ZMQ_IO_THREADS, byteps_zmq_nthreads);
    PS_VLOG(1) << "BYTEPS_ZMQ_NTHREADS set to " << byteps_zmq_nthreads;
  }

  void OfiInit() {
    // Get a list of fi_info structures for a single provider
    OfiGetProvider();
    LOG(INFO) << "Selected fabric provider is "
              << ofi_provider_->fabric_attr->prov_name;

    // Check if provider requires local memory registration
    if (ofi_provider_->domain_attr->mr_mode & FI_MR_LOCAL) {
      LOG(INFO) << "Provider " << ofi_provider_->fabric_attr->prov_name
                << " required registration of local memory buffers";
      local_mr_ = true;
      LOG(FATAL) << "Local memory region registration is not implemented";
    }
  }


  // Allocates and initialises various libfabric resources like
  // fabric, domain, endpoint, CQ and AV.
  // Returns initialised nccl_ofi_comp structure
  void OfiCreateComponent_() {
    // Determine if any tag bits are used by provider
    int ofi_tag_leading_zeroes = 0, ofi_tag_bits_for_ring_id = 64;
    while (!((ofi_provider_->ep_attr->mem_tag_format << ofi_tag_leading_zeroes++) &
      (uint64_t) OFI_HIGHEST_TAG_BIT) &&
      (ofi_tag_bits_for_ring_id >= MIN_TAG_BITS_FOR_RING_ID)) {
      ofi_tag_bits_for_ring_id--;
    }

    if (DMLC_PS_OFI_UNLIKELY(ofi_tag_bits_for_ring_id < MIN_TAG_BITS_FOR_RING_ID)) {
      LOG(FATAL) << "Provider " << ofi_provider_->fabric_attr->prov_name
                 << " does not provide enough tag bits " << ofi_tag_bits_for_ring_id
                 << " for ring ID. Minimum required is " << MIN_TAG_BITS_FOR_RING_ID;
    }

    // Set maximum tag information; Reserving 1 bit for control information
    ofi_component_->max_tag = (uint64_t)((1ULL << (ofi_tag_bits_for_ring_id - 1)) - 1);

    // Create fabric
    int ret = fi_fabric(ofi_provider_->fabric_attr, &(ofi_component_->fabric), nullptr);
    check_err(ret, "Couldn't open a fabric provider");

    // Create domain
    ret = fi_domain(ofi_component_->fabric, ofi_provider_,
        &(ofi_component_->domain), nullptr);
    check_err(ret, "Couldn't open a fabric access domain");

    /* Create transport level communication endpoint(s) */
    ret = fi_endpoint(ofi_component_->domain, ofi_provider_, &(ofi_component_->ep), nullptr);
    check_err(ret, "Couldn't allocate endpoint");

    struct fi_cq_attr cq_attr = {};
    cq_attr.format = FI_CQ_FORMAT_TAGGED;
    ret = fi_cq_open(ofi_component_->domain, &cq_attr, &ofi_component_->cq, nullptr);
    check_err(ret, "Couldn't open CQ");

    struct fi_av_attr av_attr = {};
    av_attr.type = FI_AV_TABLE;
    ret = fi_av_open(ofi_component_->domain, &av_attr, &ofi_component_->av, nullptr);
    check_err(ret, "Couldn't open AV");

    // Bind CQ and AV to endpoint
    ret = fi_ep_bind(ofi_component_->ep, (fid_t)ofi_component_->cq, FI_SEND | FI_RECV);
    check_err(ret, "Couldn't bind EP-CQ");
    ret = fi_ep_bind(ofi_component_->ep, (fid_t)ofi_component_->av, 0);
    check_err(ret, "Couldn't bind EP-CQ");

    /* Enable endpoint for communication */
    ret = fi_enable(ofi_component_->ep);
    check_err(ret, "Couldn't enable endpoint");
  }


  /*
   *  * @brief  Allocate and initialize nccl_ofi_component for the given NIC ID
   *   */
  void OfiCreateComponent() {
    // TODO: use smart pointer
    ofi_component_ = (OfiEndpoint*) calloc(1, sizeof(OfiEndpoint));
    CHECK(ofi_component_ != nullptr) << "Failed to allocate OfiEndpoint";

    /* Initialize tag and num_cqes */
    ofi_component_->tag = 1;
    ofi_component_->num_cqes = DMLC_PS_OFI_MAX_REQUESTS;
    ofi_component_->prov_name = ofi_provider_->fabric_attr->prov_name;
    OfiCreateComponent_();
    LOG(INFO) << "Created ofi component.";
  }


  void OfiListen() {
    // Create libfabric components for the given NIC,
    // if not already created, else increase tag ID.
    {
      //std::lock_guard<std::mutex> lock(mu_);
      if (DMLC_PS_OFI_UNLIKELY(ofi_component_ == nullptr)) {
        OfiCreateComponent();
      } else {
        // TODO diable this branch
        ofi_component_->tag++;
        if (ofi_component_->tag == ofi_component_->max_tag) {
          LOG(FATAL) << "Cannot open more connection for the device. Maximum is %ld"
                     << ofi_component_->max_tag;
        }
      }
      tag_ = ofi_component_->tag;
    }

    // Build handle
    int ret = fi_getname(&(ofi_component_->ep->fid), (void *)&ep_name_, &ep_name_len_);
    check_err(ret, "Call to fi_getname() failed");

    // Build listenComm
    listen_comm_ = (ListenComm *) calloc(1, sizeof(ListenComm));
    listen_comm_->tag = tag_;
    listen_comm_->local_ep = ofi_component_->ep;
    listen_comm_->accepted = false;
    // int dev = 0;
  }

  void OfiConnect(int dev, void *handle, void **sendComm) {
    ssize_t rc = 0;
    char remote_ep_addr[DMLC_PS_MAX_EP_ADDR] = {0};
    char local_ep_addr[DMLC_PS_MAX_EP_ADDR] = {0};
    size_t name_len = sizeof(local_ep_addr);
    fi_addr_t remote_addr;
    Request *req = NULL;
    size_t req_size = sizeof(Request);

    // Create libfabric components for the given NIC, if not
    // already created.
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (DMLC_PS_OFI_UNLIKELY(ofi_component_ == nullptr)) {
        OfiCreateComponent();
      }
    }
    uint64_t max_tag = ofi_component_->max_tag;
    uint64_t tag = 0ULL;

    // Parse handle to get tag and remote name
    // TODO need to store them
    //memcpy(&remote_ep_addr, (char *)handle, MAX_EP_ADDR);
    //memcpy(&tag, (char *)handle + MAX_EP_ADDR, sizeof(tag));
    CHECK(tag >= 1 && tag <= max_tag) << "Received an invalid tag "
                                      << tag << " for device";

    // Insert remote address into AV
    int ret = fi_av_insert(ofi_component_->av, (void *)remote_ep_addr, 1,
                       &remote_addr, 0, NULL);
    check_err(ret, "Unable to insert remote address into address vector for device");

    // Build SendComm
    SendComm *sComm = (SendComm *)calloc(1, sizeof(SendComm));
    CHECK(sComm != nullptr) << "Couldn't allocate SendComm";

    sComm->tag = tag;
    sComm->local_ep = ofi_component_->ep;
    sComm->remote_ep = remote_addr;

    // Pre-allocated buffers for data path
    //ret = allocate_ofi_fl(&sComm->nccl_ofi_reqs_fl, NCCL_OFI_MAX_REQUESTS,
    //          req_size);
    //if (OFI_UNLIKELY(ret != 0)) {
    //  NCCL_OFI_WARN("Could not allocate NCCL OFI requests free list for dev %d",
    //         dev);
    //  goto error;
    //}

  //  req = allocate_nccl_ofi_request(sComm->nccl_ofi_reqs_fl);
  //  if (OFI_UNLIKELY(req == NULL)) {
  //      ret = ncclSystemError;
  //      NCCL_OFI_WARN("Unable to get NCCL OFI request for device %d",
  //              sComm->dev);
  //      goto error;
  //  }

  //  req->sComm = sComm;
  //  req->direction = NCCL_OFI_SEND;

  //  /* Get local EP address to transfer in the connect message */
  //  ret = fi_getname(&(ofi_component_->ep->fid),
  //       (void *)&local_ep_addr,
  //       &name_len);
  //  if (ret != 0) {
  //    NCCL_OFI_WARN("Call to fi_getname() failed with RC: %d, ERROR: %s",
  //            ret, fi_strerror(-ret));
  //    ret = ncclSystemError;
  //    goto error;
  //  }

  //  /* Send "connect" message to remote EP */
  //  do {
  //    /*
  //    //TODO: replace it with API of FI_INJECT type when most of
  //    //   * providers can support it, so that need for completion check
  //    //        * below can be lifted.
  //    //             */
  //    rc = fi_tsend(sComm->local_ep, (void *)&local_ep_addr,
  //            MAX_EP_ADDR, NULL, sComm->remote_ep,
  //            sComm->tag | ~max_tag, &req->ctx);
  //    if (rc == 0)
  //      break;
  //    else if (rc == -FI_EAGAIN) {
  //      /*
  // *        * Process completions so that you have enough
  // *               * resources for sending connect message
  // *                      */
  //      ret = nccl_ofi_progress(ofi_component_);
  //      if (OFI_UNLIKELY(ret != 0))
  //        goto error;
  //    }
  //    else {
  //      NCCL_OFI_WARN("Unable to send connect message for dev %d. RC: %zd, ERROR: %s",
  //             dev, rc, fi_strerror(-rc));
  //      ret = ncclSystemError;
  //      goto error;
  //    }
  //  } while (true);

  //  /* Ensure the message is sent. */
  //  do {
  //    ret = nccl_ofi_progress(ofi_component_);
  //    if (OFI_UNLIKELY(ret != 0))
  //      goto error;
  //  } while (req->state != NCCL_OFI_REQ_COMPLETED);
  //
  //  *sendComm = sComm;

  //  goto exit;

  //exit:
  //  if (req)
  //    free_nccl_ofi_req(req, false);
  //  return ret;
  }



  void Stop() override {
    PS_VLOG(1) << my_node_.ShortDebugString() << " is stopping";
    // include/rdma/fabric.h:typedef uint64_t          fi_addr_t;
    Van::Stop();
    // join all threads
    should_stop_ = true;
    for (auto t : thread_list_) t->join();
    PS_VLOG(1) << my_node_.ShortDebugString() << " all threads joined and destroyed";
    // close sockets
    int linger = 0;
    int rc = zmq_setsockopt(receiver_, ZMQ_LINGER, &linger, sizeof(linger));
    CHECK(rc == 0 || errno == ETERM);
    CHECK_EQ(zmq_close(receiver_), 0);
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& it : senders_) {
      int rc = zmq_setsockopt(it.second, ZMQ_LINGER, &linger, sizeof(linger));
      CHECK(rc == 0 || errno == ETERM);
      CHECK_EQ(zmq_close(it.second), 0);
    }
    senders_.clear();
    zmq_ctx_destroy(context_);
    context_ = nullptr;
  }

  struct pair_hash {
  public:
    template <typename T, typename U>
    std::size_t operator()(const std::pair<T, U> &x) const
    {
      return std::hash<T>()(x.first) ^ std::hash<U>()(x.second);
    }
  };

  int Bind(const Node& node, int max_retry) override {
    OfiListen();

    is_worker_ = (node.role == Node::WORKER ? true : false);
    int zmq_port = ZmqBind(node, max_retry);
    if (node.role == Node::SCHEDULER) {
      auto num_instances = Postoffice::Get()->num_servers() + Postoffice::Get()->num_workers();
      for (int i = 0; i < num_instances; i++) {
        Message msg_recv;
        int bytes = ZmqRecvMsg(&msg_recv);
        LOG(INFO) << "Received " << bytes << " bytes in scheduler";

        Node key = msg_recv.meta.control.node[0];
        std::string hostname = key.hostname;
        int port = key.port;
        Node value = msg_recv.meta.control.node[1];
        std::string ep_name = value.hostname;
        int tag_id = value.port;
        // LOG(INFO) << i << ": ep_name = " << ep_name;
        LOG(INFO) << i << ": hostname = " << hostname;
        LOG(INFO) << i << ": port = " << port;
        LOG(INFO) << i << ": tag_id = " << tag_id;
        std::lock_guard<std::mutex> lock(mu_);
        endpoint_map_[std::make_pair(hostname, port)] = std::make_pair(ep_name, tag_id);
        connected_nodes_.push_back(key);
      }
      CHECK_EQ(endpoint_map_.size(), num_instances);

      Message req;
      req.meta.request = false;
      req.meta.control.cmd = Control::BOOTSTRAP;
      req.meta.app_id = 0;
      req.meta.customer_id = customer_id_;
      req.meta.addr = 0;

      // FIXME it's ok to maintain different IDs?
      int fake_id = 0;
      {
        // key
        Node key;
        key.id = Meta::kEmpty;
        // FIXME this is not accurate
        key.role = Node::WORKER;
        key.hostname = my_node_.hostname;
        key.port = zmq_port;
        // value
        Node value;
        // FIXME this is not accurate
        value.role = Node::SCHEDULER;
        std::string ep_name(ep_name_, ep_name_len_);
        value.hostname = ep_name;
        value.port = tag_;
        req.meta.control.node.push_back(key);
        req.meta.control.node.push_back(value);
      }
      for (auto k_v: endpoint_map_) {
        // key
        Node key;
        key.id = fake_id++;
        // TODO this is not accurate
        key.role = Node::WORKER;
        key.hostname = k_v.first.first;
        key.port = k_v.first.second;
        ZmqConnect(key);
        // value
        Node value;
        value.role = Node::SCHEDULER;
        value.hostname = k_v.second.first;
        value.port = k_v.second.second;
        req.meta.control.node.push_back(key);
        req.meta.control.node.push_back(value);
      }
      CHECK_EQ(fake_id, num_instances);
      // BROADCAST MAPPING TO ALL NODES
      for (int i = 0; i < num_instances; i++) {
        req.meta.recver = i;
        int bytes = ZmqSendMsg(req);
        LOG(INFO) << "Send " << bytes << " to node " << i;
      }

      while (true) {
      ;
      }

    } else {
      Node sched_node;
      sched_node.id = kScheduler;
      sched_node.port = atoi(CHECK_NOTNULL(Environment::Get()->find("DMLC_PS_ROOT_PORT")));
      sched_node.hostname = std::string(CHECK_NOTNULL(Environment::Get()->find("DMLC_PS_ROOT_URI")));
      sched_node.role = Node::SCHEDULER;
      ZmqConnect(sched_node);

      std::string ep_name(ep_name_, ep_name_len_);
      std::string hostname(node.hostname);
      // key
      Node key;
      key.role = node.role;
      key.hostname = hostname;
      key.port = zmq_port;
      // value
      Node value;
      value.role = node.role;
      value.hostname = ep_name;
      value.port = tag_;

      Message req;
      req.meta.recver = kScheduler;
      req.meta.request = false;
      req.meta.control.cmd = Control::BOOTSTRAP;
      req.meta.app_id = 0;
      req.meta.customer_id = customer_id_;

      req.meta.addr = 0;
      req.meta.control.node.push_back(key);
      req.meta.control.node.push_back(value);

      int bytes = ZmqSendMsg(req);
      LOG(INFO) << "Send " << bytes << " to the scheduler";

      Message msg_recv;
      bytes = ZmqRecvMsg(&msg_recv);
      LOG(INFO) << "Received " << bytes << " bytes from the scheduler";

      size_t kv_size = msg_recv.meta.control.node.size();
      LOG(INFO) << "Got " << kv_size << " node info ";
      CHECK_EQ(msg_recv.meta.control.node.size() % 2, 0) << "Invalid number of key value pairs";

      for (int i = 0; i < msg_recv.meta.control.node.size() / 2; i++) {
        Node recv_key = msg_recv.meta.control.node[2 * i];
        std::string recv_hostname = key.hostname;
        int recv_port = key.port;
        Node recv_value = msg_recv.meta.control.node[2 * i + 1];
        std::string recv_ep_name = value.hostname;
        int recv_tag_id = value.port;

        char readable_ep_name[DMLC_PS_MAX_EP_ADDR] = {};
        size_t readable_ep_name_len = sizeof(readable_ep_name);
        fi_av_straddr(ofi_component_->av, recv_ep_name.c_str(), readable_ep_name, &readable_ep_name_len);
        LOG(INFO) << i << ": recv_readable_ep_name_len = " << readable_ep_name_len;
        LOG(INFO) << i << ": recv_readable_ep_name = " << std::string(readable_ep_name, readable_ep_name + readable_ep_name_len);
        if (recv_port == zmq_port && recv_hostname.compare(my_node_.hostname) == 0) {
          CHECK(std::string(ep_name_, ep_name_len_).compare(recv_ep_name) == 0) << "Corrupted endpoint name detected";
          LOG(INFO) << "endpoint name consistency check passed";
        }
        LOG(INFO) << i << ": recv_hostname = " << recv_hostname;
        LOG(INFO) << i << ": recv_port = " << recv_port;
        LOG(INFO) << i << ": recv_tag_id = " << recv_tag_id;
        std::lock_guard<std::mutex> lock(mu_);
        endpoint_map_[std::make_pair(recv_hostname, recv_port)] = std::make_pair(recv_ep_name, recv_tag_id);
      }
      while (true) {
      ;
      }
      //LOG(INFO) << worker_node.DebugString();
    }

    // bootstrap
    return zmq_port;
  }

  // scheduler binds
  int ZmqBind(const Node& node, int max_retry) {
    receiver_ = zmq_socket(context_, ZMQ_ROUTER);
    int option = 1;
    CHECK(!zmq_setsockopt(receiver_, ZMQ_ROUTER_MANDATORY, &option, sizeof(option)))
        << zmq_strerror(errno);
    CHECK(receiver_ != NULL)
        << "create receiver socket failed: " << zmq_strerror(errno);
    int local = GetEnv("DMLC_LOCAL", 0);
    std::string hostname = node.hostname.empty() ? "*" : node.hostname;
    int use_kubernetes = GetEnv("DMLC_USE_KUBERNETES", 0);
    if (use_kubernetes > 0 && node.role == Node::SCHEDULER) {
      hostname = "0.0.0.0";
    }
    std::string addr = local ? "ipc:///tmp/" : "tcp://" + hostname + ":";
    int port = node.port;
    unsigned seed = static_cast<unsigned>(time(NULL) + port);
    for (int i = 0; i < max_retry + 1; ++i) {
      auto address = addr + std::to_string(port);
      if (zmq_bind(receiver_, address.c_str()) == 0) break;
      if (i == max_retry) {
        port = -1;
      } else {
        port = 10000 + rand_r(&seed) % 40000;
      }
    }
    std::lock_guard<std::mutex> lk(mu_);
    auto t = new std::thread(&FabricVan2::ZmqSchedulerThread, this, (void*) receiver_);
    thread_list_.push_back(t);
    LOG(INFO) << "ZMQ scheduler bound to port " << port;
    return port;
  }

  void Connect(const Node& node) override {
    // use zmq to boostrap libfabric
    CHECK_NE(node.id, node.kEmpty);
    // connect to scheduler
    // connect to worker / server
  }

  void ZmqConnect(const Node& node) {
    // use zmq to boostrap libfabric
    CHECK_NE(node.id, node.kEmpty);
    CHECK_NE(node.port, node.kEmpty);
    CHECK(node.hostname.size());
    int id = node.id;
    mu_.lock();
    auto it = senders_.find(id);
    if (it != senders_.end()) {
      zmq_close(it->second);
    }
    mu_.unlock();
    // worker doesn't need to connect to the other workers. same for server
    if ((node.role == my_node_.role) && (node.id != my_node_.id)) {
      return;
    }
    void* sender = zmq_socket(context_, ZMQ_DEALER);
    CHECK(sender != NULL)
        << zmq_strerror(errno)
        << ". it often can be solved by \"sudo ulimit -n 65536\""
        << " or edit /etc/security/limits.conf";
    if (my_node_.id != Node::kEmpty) {
      std::string my_id = "ps" + std::to_string(my_node_.id);
      zmq_setsockopt(sender, ZMQ_IDENTITY, my_id.data(), my_id.size());
      std::lock_guard<std::mutex> lk(mu_);
      if (is_worker_ && (senders_.find(id)==senders_.end())) {
        auto t = new std::thread(&FabricVan2::ZmqNonSchedulerThread, this, (void*) sender);
        thread_list_.push_back(t);
      }
    }
    // connect
    std::string addr =
        "tcp://" + node.hostname + ":" + std::to_string(node.port);
    if (GetEnv("DMLC_LOCAL", 0)) {
      addr = "ipc:///tmp/" + std::to_string(node.port);
    }
    if (zmq_connect(sender, addr.c_str()) != 0) {
      LOG(FATAL) << "connect to " + addr + " failed: " + zmq_strerror(errno);
    }
    std::lock_guard<std::mutex> lk(mu_);
    senders_[id] = sender;
    if (my_node_.role != Node::SCHEDULER) {
      CHECK_EQ(senders_.size(), 1) << "Unexpected number of senders";
    }
    LOG(INFO) << "ZMQ sender " << id << " connected to " + addr;
  }

  int SendMsg(Message& msg) override {
    return 0;
  }

  int ZmqSendMsg(Message& msg) {
    //if (!is_worker_) return NonWorkerSendMsg(msg);

    std::lock_guard<std::mutex> lk(mu_);

    int id = msg.meta.recver;
    CHECK_NE(id, Meta::kEmpty);

    // find the socket
    auto it = senders_.find(id);
    if (it == senders_.end()) {
      LOG(WARNING) << "there is no socket to node " << id;
      return -1;
    }

    void* socket = it->second;

    return ZmqSendMsg(socket, msg);
  }

  int RecvMsg(Message* msg) override {
    return -1;
  }

  int ZmqRecvMsg(Message* msg) {
    msg->data.clear();

    ZmqBufferContext2 notification;
    recv_buffers_.WaitAndPop(&notification);

    size_t recv_bytes = 0;

    msg->meta.sender = notification.sender;
    msg->meta.recver = my_node_.id;

    char* meta_buf = CHECK_NOTNULL((char*)zmq_msg_data(notification.meta_zmsg));
    size_t meta_len = zmq_msg_size(notification.meta_zmsg);

    UnpackMeta(meta_buf, meta_len, &(msg->meta));
    recv_bytes += meta_len;

    //for (size_t i = 0; i < notification.data_zmsg.size(); ++i) {
    //  auto zmsg = notification.data_zmsg[i];
    //  char* buf = CHECK_NOTNULL((char*)zmq_msg_data(zmsg));
    //  size_t size = zmq_msg_size(zmsg);
    //  recv_bytes += size;


    //  SArray<char> data;
    //  // zero copy
    //  data.reset(buf, size, [zmsg, size](void *) {
    //    zmq_msg_close(zmsg);
    //    delete zmsg;
    //  });
    //  msg->data.push_back(data);
    //}

    return recv_bytes;
  }

 private:
   void OfiGetProvider() {
     struct fi_info *hints = fi_allocinfo();
     CHECK(hints != nullptr) << "Unable to allocate fi_info";
    
     // Hints to filter providers
     // TODO remove FI_TAGGED?
     hints->caps = FI_TAGGED | FI_MSG;
     hints->mode = FI_CONTEXT;

     hints->ep_attr->type = FI_EP_RDM;

     hints->domain_attr->av_type = FI_AV_TABLE;
     hints->domain_attr->control_progress = FI_PROGRESS_AUTO;
     hints->domain_attr->data_progress = FI_PROGRESS_AUTO;

     // Indicate that the application support local memory registration
     hints->domain_attr->mr_mode = FI_MR_LOCAL;

     // TODO figure out msg_order
     hints->tx_attr->msg_order = FI_ORDER_SAS;
     hints->rx_attr->msg_order = FI_ORDER_SAS;

     int ret = 0;
     ret = fi_getinfo(dmlc_ps_ofi_version, nullptr, 0, 0, hints, &ofi_provider_);
     if (ret == -FI_ENODATA) {
       LOG(FATAL) << "Could not find any optimal provider.";
     } else {
       check_err(ret, "Could not complete fi_getinfo");
     }
     fi_freeinfo(hints);
     CHECK(ofi_provider_ != nullptr) << "Failed to get ofi provider";
     // If we detect the Amazon EFA provider, emulate a NIC per GPU
     // so that NCCL will build more rings and achieve better peak BW
     if (IS_EFA_PROVIDER(ofi_provider_->fabric_attr->prov_name)) {
       ofi_provider_->next = ofi_provider_;
     }
  }


  int NonWorkerSendMsg(Message& msg) {
    std::lock_guard<std::mutex> lk(mu_);

    // find the socket
    int id = msg.meta.recver;
    CHECK_NE(id, Meta::kEmpty);

    auto it = senders_.find(id);
    if (it == senders_.end()) {
      LOG(WARNING) << "there is no socket to node " << id;
      return -1;
    }

    void* socket;

    if (msg.meta.simple_app || !msg.meta.control.empty()
               || (GetRoleFromId(id) != Node::WORKER)) {
      socket = it->second;
    }
    else { // data msg, and recver is WORKER
      socket = receiver_; // scheduler/server using receiver socket --> worker sender socket

      // first, send dst id
      std::string dst = "ps" + std::to_string(id);
      int len = dst.size();
      char *dst_array = new char[len + 1];
      strcpy(dst_array, dst.c_str());
      CHECK(dst_array);

      zmq_msg_t zmsg_dstid;
      CHECK_EQ(zmq_msg_init_data(
          &zmsg_dstid, dst_array, len, FreeData2, NULL), 0);
      while (true) {
        if (len == zmq_msg_send(&zmsg_dstid, receiver_, ZMQ_SNDMORE)) break;
        if (errno == EINTR) continue;
        CHECK(0) << zmq_strerror(errno);
      }

      // second, send my id
      std::string my_id = "ps" + std::to_string(my_node_.id);
      len = my_id.size();
      char *myid_array = new char[len + 1];
      strcpy(myid_array, my_id.c_str());
      CHECK(myid_array);

      zmq_msg_t zmsg_myid;
      CHECK_EQ(zmq_msg_init_data(
          &zmsg_myid, myid_array, len, FreeData2, NULL), 0);
      while (true) {
        if (len == zmq_msg_send(&zmsg_myid, receiver_, ZMQ_SNDMORE)) break;
        if (errno == EINTR) continue;
        CHECK(0) << zmq_strerror(errno);
      }
    }

    return ZmqSendMsg(socket, msg);
  }

  void ZmqSchedulerThread(void* socket) {
    CHECK(socket);
    LOG(INFO) << "Start ZMQ recv thread";

    while (true) {
      ZmqBufferContext2 *buf_ctx = new ZmqBufferContext2();

      for (int i = 0;; ++i) {
        zmq_msg_t* zmsg = new zmq_msg_t;
        CHECK(zmq_msg_init(zmsg) == 0) << zmq_strerror(errno);
        while (true) {
          std::lock_guard<std::mutex> lk(mu_);
          // the zmq_msg_recv should be non-blocking, otherwise deadlock will happen
          int tag = ZMQ_DONTWAIT;
          if (should_stop_ || zmq_msg_recv(zmsg, socket, tag) != -1) break;
          if (errno == EINTR) {
            std::cout << "interrupted";
            continue;
          } else if (errno == EAGAIN) { // ZMQ_DONTWAIT
            continue;
          }
          CHECK(0) << "failed to receive message. errno: " << errno << " "
                       << zmq_strerror(errno);
        }
        if (should_stop_) break;
        char* buf = CHECK_NOTNULL((char*)zmq_msg_data(zmsg));
        size_t size = zmq_msg_size(zmsg);

        if (i == 0) {
          // identify
          buf_ctx->sender = GetNodeID(buf, size);
          CHECK(zmq_msg_more(zmsg));
          zmq_msg_close(zmsg);
          delete zmsg;
        }
        else if (i == 1) {
          // task
          buf_ctx->meta_zmsg = zmsg;
          bool more = zmq_msg_more(zmsg);
          if (!more) break;
        }
        else {
          buf_ctx->data_zmsg.push_back(zmsg);
          bool more = zmq_msg_more(zmsg);
          if (!more) break;
        }
      } // for
      if (should_stop_) break;
      recv_buffers_.Push(*buf_ctx);
    } // while
  }

  void ZmqNonSchedulerThread(void* socket) {
    CHECK(socket);
    LOG(INFO) << "Start ZMQ recv thread";

    while (true) {
      ZmqBufferContext2 *buf_ctx = new ZmqBufferContext2();

      for (int i = 0;; ++i) {
        zmq_msg_t* zmsg = new zmq_msg_t;
        CHECK(zmq_msg_init(zmsg) == 0) << zmq_strerror(errno);
        while (true) {
          std::lock_guard<std::mutex> lk(mu_);
          // the zmq_msg_recv should be non-blocking, otherwise deadlock will happen
          int tag = ZMQ_DONTWAIT;
          if (should_stop_ || zmq_msg_recv(zmsg, socket, tag) != -1) break;
          if (errno == EINTR) {
            std::cout << "interrupted";
            continue;
          } else if (errno == EAGAIN) { // ZMQ_DONTWAIT
            continue;
          }
          CHECK(0) << "failed to receive message. errno: " << errno << " "
                       << zmq_strerror(errno);
        }
        if (should_stop_) break;
        char* buf = CHECK_NOTNULL((char*)zmq_msg_data(zmsg));
        size_t size = zmq_msg_size(zmsg);

        if (i == 0) {
          // identify
          buf_ctx->sender = GetNodeID(buf, size);
          CHECK(zmq_msg_more(zmsg));
          zmq_msg_close(zmsg);
          delete zmsg;
        }
        else if (i == 1) {
          // task
          buf_ctx->meta_zmsg = zmsg;
          bool more = zmq_msg_more(zmsg);
          if (!more) break;
        }
        else {
          buf_ctx->data_zmsg.push_back(zmsg);
          bool more = zmq_msg_more(zmsg);
          if (!more) break;
        }
      } // for
      if (should_stop_) break;
      recv_buffers_.Push(*buf_ctx);
    } // while
  }

  int ZmqSendMsg(void* socket, Message& msg) {
    // send meta
    int meta_size;
    char* meta_buf = nullptr;
    PackMeta(msg.meta, &meta_buf, &meta_size);
    int tag = ZMQ_SNDMORE;
    int n = msg.data.size();
    if (n == 0) tag = 0;
    zmq_msg_t meta_msg;
    zmq_msg_init_data(&meta_msg, meta_buf, meta_size, FreeData2, NULL);
    while (true) {
      if (zmq_msg_send(&meta_msg, socket, tag) == meta_size) break;
      if (errno == EINTR) continue;
      CHECK(0) << zmq_strerror(errno);
    }
    zmq_msg_close(&meta_msg);
    int send_bytes = meta_size;
    // send data
    //for (int i = 0; i < n; ++i) {
    //  zmq_msg_t data_msg;
    //  SArray<char>* data = new SArray<char>(msg.data[i]);
    //  int data_size = data->size();
    //  zmq_msg_init_data(&data_msg, data->data(), data->size(), FreeData2, data);
    //  if (i == n - 1) tag = ZMQ_DONTWAIT;
    //  while (true) {
    //    if (zmq_msg_send(&data_msg, socket, tag) == data_size) break;
    //    if (errno == EINTR) continue;
    //    LOG(WARNING) << "failed to send message, errno: "
    //                 << errno << " " << zmq_strerror(errno)
    //                 << ". " << i << "/" << n;
    //    return -1;
    //  }
    //  zmq_msg_close(&data_msg);
    //  send_bytes += data_size;
    //}
    return send_bytes;
  }

  /**
   * return the node id given the received identity
   * \return -1 if not find
   */
  int GetNodeID(const char* buf, size_t size) {
    if (size > 2 && buf[0] == 'p' && buf[1] == 's') {
      int id = 0;
      size_t i = 2;
      for (; i < size; ++i) {
        if (buf[i] >= '0' && buf[i] <= '9') {
          id = id * 10 + buf[i] - '0';
        } else {
          break;
        }
      }
      if (i == size) return id;
    }
    return Meta::kEmpty;
  }

  Node::Role GetRoleFromId(int id) {
    if (id < 8) return Node::SCHEDULER;
    if (id % 2) return Node::WORKER;
    return Node::SERVER;
  }

  void* context_ = nullptr;
  /**
   * \brief node_id to the socket for sending data to this node
   */
  std::unordered_map<int, void*> senders_;
  std::mutex mu_;
  void* receiver_ = nullptr;

  bool is_worker_;

  // Recv buffer queue
  ThreadsafeQueue<ZmqBufferContext2> recv_buffers_;

  std::atomic<bool> should_stop_{false};

  std::vector<std::thread*> thread_list_;

  // NICs info list for a provider
  struct fi_info* ofi_provider_ = nullptr;
  // Indicates if memory registration of local buffers is required
  bool local_mr_ = false;
  // NCCL OFI component array for all NICs
  OfiEndpoint* ofi_component_ = nullptr;
  // name of the endpoint
  char ep_name_[DMLC_PS_MAX_EP_ADDR] = {};
  // length of the name
  size_t ep_name_len_ = sizeof(ep_name_);
  //  endpoint tag
  uint64_t tag_;
  // listen comm
  ListenComm* listen_comm_ = nullptr;
  // send_comms_ /**
  // * \brief node_id to the socket for sending data to this node
  // */
  //std::unordered_map<int, void*> senders_;
  //std::unique_ptr<Van> zmq_van_;
  //ZMQVan* zmq_van_ = new std::unique_ptr<ZMQVan>;
  int customer_id_;
  // hostname to (endpoint, tag)
  std::unordered_map<std::pair<std::string, int>, std::pair<std::string, uint64_t>, pair_hash> endpoint_map_;
  std::vector<Node> connected_nodes_;
};
}  // namespace ps

#endif  // PS_FABRIC_VAN2_H_

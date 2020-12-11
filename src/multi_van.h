
// Copyright 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================

#ifndef PS_MULTI_VAN_H_
#define PS_MULTI_VAN_H_

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <zmq.h>
#include <sys/uio.h>

#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "ps/internal/threadsafe_queue.h"
#include "ps/internal/van.h"
#include "van_common.h"

namespace ps {

const int ID_OFFSET = 1000000;
const int MAX_NUM_IDS = 1000;

class MultiVanBufferContext {
public:
  int msg_len;
  int src_idx;
  Message msg;
};

class MultiVan : public Van {
 public:
  MultiVan() {
    num_ports_ = 1;
    const char *npstr = Environment::Get()->find("DMLC_NUM_PORTS");
    if (npstr) num_ports_ = atoi(npstr);
  }

  ~MultiVan() { PS_VLOG(1) << "~MultiVan"; }

  virtual std::string GetType() const {
    return std::string("multivan");
  }

 protected:
  void Start(int customer_id, bool standalone) override {
    start_mu_.lock();
    should_stop_ = false;
    should_stop_polling_ = false;

    auto val = Environment::Get()->find("DMLC_ROLE");
    std::string role(val);
    LOG(INFO) << "This is a " << role;
    start_mu_.unlock();

    // van creation
    for (int i = 0; i < num_ports_; ++i) {
      auto van = Van::Create("zmq");
      van->Start(customer_id, true);
      vans_.push_back(van);
    }
    Van::Start(customer_id, false);
  }

  void Stop() override {
    PS_VLOG(1) << "Stopping " << my_node_.ShortDebugString();
    // stop zmq vans
    for (auto van : vans_) {
      van->Stop();
    }

    should_stop_ = true;
    Van::Stop();

    PS_VLOG(1) << "Stopping polling_threads_";
    for (auto& thread : polling_threads_) {
      thread->join();
      thread.reset();
    }
  }

  int Bind(Node& node, int max_retry) override {
    std::lock_guard<std::mutex> lk(endpoints_mu_);
    CHECK_EQ(node.num_ports, num_ports_);
    my_nodes_.resize(num_ports_);
    for (int i = 0; i < num_ports_; ++i) {
      int port = node.ports[i];
      Node one_node = node;
      one_node.id = EncodeManagedID(node.id, i);
      one_node.num_ports = 1;
      one_node.ports[0] = port;
      one_node.port = node.port;
      int bound_port = vans_[i]->Bind(one_node, max_retry);
      node.ports[i] = bound_port;
      polling_threads_.emplace_back(new std::thread(&MultiVan::PollingThread, this, i));
      my_nodes_[i] = one_node;
    }
    return node.ports[0];
  }

  void Connect(const Node &node) override {
    CHECK_NE(node.id, node.kEmpty);
    CHECK_NE(node.port, node.kEmpty);
    CHECK(node.hostname.size());

    // worker doesn't need to connect to the other workers. same for server
    if ((node.role == my_node_.role) && (node.id != my_node_.id)) {
      PS_VLOG(3) << "Multivan skipped connection to " << node.DebugString()
                 << ". My node is " << my_node_.DebugString();
      return;
    }
    for (int i = 0; i < num_ports_; ++i) {
      for (int j = 0; j < node.num_ports; ++j) {
        // connect each van pair
        Node one_node = node;
        // the underlying van uses id: 0001, 1001, 2001, etc
        one_node.id = EncodeManagedID(node.id, j);
        one_node.num_ports = 1;
        one_node.port = node.ports[j];
        one_node.ports[0] = one_node.port;
        PS_VLOG(3) << "Connect: " << one_node.DebugString() << " from " << my_nodes_[i].DebugString();
        vans_[i]->Connect(one_node);
      }
    }
  }

  int SendMsg(Message &msg) override {
    int remote_id = msg.meta.recver;
    CHECK_NE(remote_id, Node::kEmpty);
    // XXX assume device IDs are: [0, 1 ... num_ports - 1]
    bool pushpull = IsValidPushpull(msg);
    int src_idx = 0;
    int dst_idx = 0;
    if (pushpull && msg.data.size() == 3) {
      auto& data = msg.data[1];
      CHECK(data.src_device_type_ == CPU);
      CHECK(data.dst_device_type_ == CPU);
      src_idx = data.src_device_id_;
      dst_idx = data.dst_device_id_;
    }
    Message van_msg = msg;
    auto van = vans_[src_idx];
    van_msg.meta.sender = EncodeManagedID(msg.meta.sender, src_idx);
    van_msg.meta.recver = EncodeManagedID(msg.meta.recver, dst_idx);
    PS_VLOG(3) << "SendMsg: " << van_msg.DebugString();
    return van->SendMsg(van_msg);
  }

  int RecvMsg(Message *msg) override {
    msg->data.clear();
    MultiVanBufferContext ctx;
    recv_buffers_.WaitAndPop(&ctx);
    PS_VLOG(3) << "RecvMsg: " << ctx.msg.DebugString();
    ctx.msg.meta.sender = DecodeMangedID(ctx.msg.meta.sender);
    ctx.msg.meta.recver = DecodeMangedID(ctx.msg.meta.recver);
    *msg = ctx.msg;
    return ctx.msg_len;
  }

  inline void SetNode(const Node& node) {
    my_node_ = node;
    my_nodes_.resize(num_ports_);
    for (int i = 0; i < num_ports_; ++i) {
      Node one_node = node;
      one_node.id = EncodeManagedID(node.id, i);
      one_node.num_ports = 1;
      one_node.ports[0] = node.ports[i];
      one_node.port = node.ports[i];
      vans_[i]->SetNode(one_node);
      my_nodes_[i] = one_node;
    }
  }

 private:
  int EncodeManagedID(int id, int index) {
    if (id == Node::kEmpty) {
      return Node::kEmpty;
    }
    CHECK(id < MAX_NUM_IDS);
    return ID_OFFSET + id + index * MAX_NUM_IDS;
  }

  int DecodeMangedID(int managed_id) {
    if (managed_id == Node::kEmpty) {
      return managed_id;
    }
    managed_id -= ID_OFFSET;
    int id = managed_id % MAX_NUM_IDS;
    return id;
  }


  void PollingThread(int index) {
    while (!should_stop_polling_) {
      Message msg;
      int recv_bytes = vans_[index]->RecvMsg(&msg);
      MultiVanBufferContext ctx;
      ctx.msg_len = recv_bytes;
      ctx.src_idx = index;
      ctx.msg = msg;
      recv_buffers_.Push(ctx);
    }
    PS_VLOG(3) << "PollingThread exited";
  }

  // stop signals
  std::atomic<bool> should_stop_;
  // stop signal for the non-blocking polling thread
  std::atomic<bool> should_stop_polling_;
  std::mutex endpoints_mu_;

  // event thread
  std::vector<std::unique_ptr<std::thread>> polling_threads_;
  // Recv buffer queue
  ThreadsafeQueue<MultiVanBufferContext> recv_buffers_;

  std::vector<Van*> vans_;

  bool is_worker_;
  int num_ports_;
  std::vector<Node> my_nodes_;

};  // FabricVan
};  // namespace ps


#endif  // PS_FABRIC_VAN_H_

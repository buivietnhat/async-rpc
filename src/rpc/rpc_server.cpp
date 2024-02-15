#include "rpc/rpc_server.h"

#include <iostream>
#include <mutex>

#include "common/marco.h"
#include "rpc/marshall.h"

namespace rpc {

RpcServer::RpcServer(std::unique_ptr<Acceptor> acceptor, std::unique_ptr<IoContext> ctx, int num_worker)
    : ctx_(std::move(ctx)), acceptor_(std::move(acceptor)), num_worker_(num_worker) {
  nonce_ = random();
  Register(BIND, this, &RpcServer::Bind);
}

void RpcServer::Register(proc_t proc, Handler &&handler) {
  std::lock_guard l(procs_mu_);
  assert(!procs_.contains(proc));
  procs_[proc] = std::move(handler);
}

void RpcServer::AcceptNewConnection() {
  acceptor_->Accept(
      [me = shared_from_this()](std::unique_ptr<Socket> socket, Err e) { me->OnNewConnection(std::move(socket), e); });
}

void RpcServer::Dispatch(std::shared_ptr<Socket> socket, std::string &&buf) {
  if (socket == nullptr) {
    throw std::runtime_error("dispatch with null socket");
  }

  Unmarshall req(std::move(buf));
  RequestHeader h;
  req >> h;
  auto proc = h.proc_;

  auto rep = std::make_shared<Marshall>();
  ReplyHeader rh{h.xid_, OK};

  // is the client sending to an old instane of server?
  if (h.srv_nonce_ != 0 && h.srv_nonce_ != nonce_) {
    std::cout << "rpcs: dispach rpc for old instance" << std::endl;
    rh.err_ = OLDSERVER_FAILURE;
    *rep << rh;
    socket->Send(rep->Buf(),
                 [me = shared_from_this(), rep, s = socket](size_t n, Err e) { me->OnSendFinished(s, rep, n, e); });
    return;
  }

  Handler *handler;
  {
    std::lock_guard l(procs_mu_);
    if (!procs_.contains(proc)) {
      std::cerr << "rpcs: dispatch: unknown proc " << proc << std::endl;
      throw std::runtime_error("rpcs: dispatch: unknown proc");
    }
    handler = &procs_[proc];
  }

  State state = NEW;
  std::string *rep_buf;

  if (h.clt_nonce_ > 0) {
    // have I seen this client before?
    {
      std::lock_guard l(rep_window_mu_);
      // if we don't know about this clt_nonce, create a cleanup object
      if (reply_window_.find(h.clt_nonce_) == reply_window_.end()) {
        reply_window_[h.clt_nonce_] = {};
      }
    }

    state = CheckDuplicateAndUpdate(h.clt_nonce_, h.xid_, h.xid_rep_, &rep_buf);
  }

  switch (state) {
    case State::NEW: {
      rh.err_ = (*handler)(req, *rep);
      *rep << rh;

      if (h.clt_nonce_ > 0) {
        // only recordd replies for clients that require at-most-once logic
        AddReply(h.clt_nonce_, h.xid_, rep->Buf());
      }

      socket->Send(rep->Buf(),
                   [me = shared_from_this(), rep, s = socket](size_t n, Err e) { me->OnSendFinished(s, rep, n, e); });
      break;
    }
    case State::INPROGRESS:  // server is working on this request
      std::cout << "RPCS: dulicated request from client: " << h.clt_nonce_ << ", xid = " << h.xid_
                << ", but no "
                   "response yet"
                << std::endl;
      ReadIncomingPacket(socket);
      break;
    case State::DONE:  // duplicate and we still have the response
      std::cout << "RPCS: dulicated request from client: " << h.clt_nonce_ << ", xid = " << h.xid_
                << ", and I have a response " << std::endl;
      assert(rep_buf != nullptr);
      socket->Send(*rep_buf, [me = shared_from_this(), s = socket](size_t, Err) {
        me->ReadIncomingPacket(s);
      });
      break;
    case State::FORGOTTEN:
      std::cout << "RPC Server dispatch: very old request " << h.xid_ << " from " << h.clt_nonce_ << std::endl;
      rh.err_ = ATMOST_ONCE_FAILURE;
      *rep << rh;
      socket->Send(rep->Buf(), [me = shared_from_this(), s = socket](size_t, Err) {
        me->ReadIncomingPacket(s);
      });
      break;
  }
}

void RpcServer::ReadIncomingPacket(std::shared_ptr<Socket> socket) {
  auto data = std::make_shared<std::string>(DEFAULT_RPC_SZ, ' ');
  socket->Receive(*data, [me = shared_from_this(), data, s = socket](size_t n, Err e) mutable {
    me->OnReceiveFinished(s, data, n, e);
  });
}

void RpcServer::Start() {
  AcceptNewConnection();

  workers_.reserve(num_worker_);
  for (int i = 0; i < num_worker_; i++) {
    workers_.push_back(std::thread([&] { ctx_->Run(); }));
  }
}

void RpcServer::FreeReplyWindow() {}

RpcServer::State RpcServer::CheckDuplicateAndUpdate(uint32_t clt_nonce, xid_t xid, xid_t xid_rep,
                                                    std::string **rep_buf) {
  std::lock_guard l(rep_window_mu_);

  if (xid_rep > max_ackowledged_rep_[clt_nonce]) {
    max_ackowledged_rep_[clt_nonce] = xid_rep;
  }

  // trim the window of remembered requests and reply values on exitting
  ON_SCOPE_EXIT {
    auto &rw = reply_window_[clt_nonce];
    for (auto it = rw.begin(); it != rw.end();) {
      if (it->xid_ <= max_ackowledged_rep_[clt_nonce]) {
        it = rw.erase(it);
      } else {
        break;
      }
    }
  };

  // 1) check if the request is already forgotten
  if (xid <= max_ackowledged_rep_[clt_nonce]) {
    return FORGOTTEN;
  }

  auto &rep_window = reply_window_[clt_nonce];
  for (auto it = rep_window.begin(); it != rep_window.end(); it++) {
    // 2) if we have rememberd the request
    if (it->xid_ == xid) {
      if (it->cb_present) {
        *rep_buf = &it->buf_;
        return DONE;
      }
      return INPROGRESS;
    }

    // 3) we never saw the request before
    if (it->xid_ > xid) {
      rep_window.insert(it, ReplyT(xid));
      return NEW;
    }
  }
  rep_window.push_back(ReplyT(xid));
  return NEW;
}

void RpcServer::AddReply(uint32_t client_nonce, xid_t xid, const std::string &rep_buf) {
  std::lock_guard l(rep_window_mu_);
  if (reply_window_.count(client_nonce) == 0) {
    throw std::runtime_error("add to on-existed reply");
  }

  for (auto &rep : reply_window_[client_nonce]) {
    if (rep.xid_ == xid) {
      std::cout << "RCPS: add reply for client " << client_nonce << ", xid = " << xid << std::endl;
      rep.cb_present = true;
      rep.buf_ = rep_buf;
      return;
    }
  }

  throw std::runtime_error("unreachable");
}

}  // namespace rpc
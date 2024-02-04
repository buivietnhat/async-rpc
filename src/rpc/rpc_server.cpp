#include "rpc/rpc_server.h"

#include <iostream>
#include <mutex>

#include "rpc/marshall.h"

namespace rpc {

RpcServer::RpcServer(std::unique_ptr<Acceptor> acceptor, std::unique_ptr<IoContext> ctx, int num_worker)
    : ctx_(std::move(ctx)), acceptor_(std::move(acceptor)), num_worker_(num_worker) {
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

  rh.err_ = (*handler)(req, *rep);
  *rep << rh;
  std::cout << "Rep buf: " << rep->Buf() << std::endl;
  socket->Send(rep->Buf(),
               [me = shared_from_this(), rep, s = socket](size_t n, Err e) { me->OnSendFinished(s, rep, n, e); });
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

}  // namespace rpc
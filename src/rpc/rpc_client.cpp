#include "rpc/rpc_client.h"

#include <iostream>
#include <mutex>

#include "rpc/marshall.h"

namespace rpc {

inline void SetRandSeed() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  srandom((int)ts.tv_nsec ^ ((int)getpid()));
}

RpcClient::Caller::Caller(xid_t xid, Callback &&cb, ReplyExtractor &&re)
    : xid_(xid), cb_(std::move(cb)), re_(std::move(re)) {}

RpcClient::RpcClient(std::unique_ptr<IoContext> ctx, std::unique_ptr<Socket> socket, std::string_view host,
                     std::string_view port, int num_worker, bool retrans)
    : socket_(std::move(socket)), ctx_(std::move(ctx)) {
  if (retrans) {
    SetRandSeed();
    clt_nonce_ = random();
  }

  if (!socket_ || !ctx_) {
    throw std::runtime_error("RpcClient constructor: null socket or ctx");
  }

  socket_->Connect(host, port, [&](size_t, Err e) {
    if (e != Err::OK) {
      std::cout << "Err couldn't connect to server : " << ToString(e) << std::endl;
      throw std::runtime_error("cannot connect to server");
    }

    OnConnected();
  });

  workers_.reserve(num_worker);
  for (int i = 0; i < num_worker; i++) {
    workers_.push_back(std::thread([&] { ctx_->Run(); }));
  }
}

Err RpcClient::Call(proc_t proc, Marshall &&req, Callback &&cb, ReplyExtractor &&re) {
  auto c = std::make_unique<Caller>(0, std::move(cb), std::move(re));
  auto c_ptr = c.get();
  {
    std::scoped_lock l(callers_mu_);
    c->xid_ = xid_++;
    callers_[c->xid_] = std::move(c);
  }

  RequestHeader h{c_ptr->xid_, proc, clt_nonce_, srv_nonce_};
  req << h;

  c_ptr->SetBuf(req.Buf());
  socket_->Send(c_ptr->Buf(),
                [me = shared_from_this(), xid = c_ptr->xid_](size_t n, Err e) { me->OnSendFinished(xid, n, e); });

  return OK;
}

Err RpcClient::Bind(TimeOut to) {
  int reply;
  //  auto err = Call(BIND, to, reply, 0);
  //  if (err != OK) {
  //    std::cout << "failed to call Bind" << std::endl;
  //    return err;
  //  }
  //
  //  srv_nonce_ = reply;
  return OK;
}

void RpcClient::GotPdu(std::string &&buf) {
  Unmarshall u(std::move(buf));
  ReplyHeader h;
  u >> h;
  //  if (!u.Ok()) {
  //    std::cout << "GotPdu unmarshall header failed";
  //    return;
  //  }

  auto c = [&]() -> std::unique_ptr<Caller> {
    {
      std::scoped_lock l(callers_mu_);
      if (!callers_.contains(h.xid_)) {
        std::cout << "got pdu xid " << h.xid_ << " but no pending request" << std::endl;
        return {};
      }
      std::cout << "got reply for xid " << h.xid_ << std::endl;
      auto c = std::move(callers_[h.xid_]);
      callers_.erase(h.xid_);
      return c;
    }
  }();

  if (!c) {
    return;
  }

  Err e = Err::OK;
  {
    std::unique_lock l(c->mu_);
    if (!c->done_) {
      c->err_ = h.err_;
      if (c->err_ != OK) {
        std::cout << "got pdu: RPC reply error for xid " << h.xid_ << ", err = " << ToString(c->err_) << std::endl;
      }
      c->done_ = true;
    }
    e = c->err_;
  }

  c->cb_(c->re_.ExtractReply(u), e);
}

void RpcClient::ListenFromServer() {
  auto data = std::make_shared<std::string>(DEFAULT_RPC_SZ, ' ');
  socket_->Receive(*data, [me = shared_from_this(), data](size_t n, Err e) { me->OnReceiveFinished(data, n, e); });
}

}  // namespace rpc
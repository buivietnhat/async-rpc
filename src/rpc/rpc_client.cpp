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

RpcClient::Caller::Caller(xid_t xid, Callback &&cb, ReplyExtractor &&re, std::unique_ptr<Timer> deadline_timer)
    : xid_(xid), cb_(std::move(cb)), re_(std::move(re)), timer_(std::move(deadline_timer)) {}

RpcClient::RpcClient(std::unique_ptr<IoContext> ctx, std::unique_ptr<Socket> socket, std::string_view host,
                     std::string_view port, int num_worker, bool retrans)
    : socket_(std::move(socket)), host_(host), port_(port), ctx_(std::move(ctx)), num_worker_(num_worker) {
  if (retrans) {
    SetRandSeed();
    clt_nonce_ = random();
  }

  if (!socket_ || !ctx_) {
    throw std::runtime_error("RpcClient constructor: null socket or ctx");
  }
}

Err RpcClient::Call(proc_t proc, Marshall &&req, Callback &&cb, ReplyExtractor &&re, TimeOut to) {
  auto timer = ctx_->CreateTimer(to.ms_);
  auto timer_ptr = timer.get();

  auto c = std::make_unique<Caller>(0, std::move(cb), std::move(re), std::move(timer));
  auto *c_ptr = c.get();
  {
    std::scoped_lock l(callers_mu_);
    if ((proc != BIND && !bind_done_) || (proc == BIND && bind_done_)) {
      std::cout << "Call: rpcc has not been bound to dst or binding twice" << std::endl;
      return BIND_FAILURE;
    }

    c->xid_ = xid_++;
    callers_[c->xid_] = std::move(c);
  }

  auto received_xid = [&]() -> xid_t {
    std::lock_guard l(rep_window_mu_);
    if (xid_rep_window_.empty()) {
      return 0;
    }
    return xid_rep_window_.front();
  }();

  RequestHeader h{c_ptr->xid_, proc, clt_nonce_, srv_nonce_, received_xid};
  req << h;

  c_ptr->SetBuf(req.Buf());
  socket_->Send(c_ptr->Buf(),
                [me = shared_from_this(), xid = c_ptr->xid_](size_t n, Err e) { me->OnSendFinished(xid, n, e); });

  timer_ptr->Wait([&, c_ptr](Err e) { OnTimerExpired(c_ptr, e); });

  return OK;
}

Err RpcClient::Bind(TimeOut to) {
  Call<int, uint32_t>(
      BIND, 0,
      [me = shared_from_this()](std::any reply, Err e) {
        if (e != OK) {
          std::cout << "failed to call Bind, retry after 200 miliseconds ..." << std::endl;
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          me->Bind();
        } else {
          std::scoped_lock l(me->callers_mu_);
          me->srv_nonce_ = std::any_cast<uint32_t>(reply);
          me->bind_done_ = true;
        }
      },
      to);

  return OK;
}

void RpcClient::GotPdu(std::string &&buf) {
  Unmarshall u(std::move(buf));
  ReplyHeader h;
  u >> h;

  UpdateXidRep(h.xid_);

  auto c = [&]() -> std::unique_ptr<Caller> {
    {
      std::scoped_lock l(callers_mu_);
      if (!callers_.contains(h.xid_)) {
        std::cout << "RPCC: got pdu xid " << h.xid_ << " but no pending request" << std::endl;
        return {};
      }
      //      std::cout << "got reply for xid " << h.xid_ << std::endl;
      auto c = std::move(callers_[h.xid_]);
      callers_.erase(h.xid_);
      return c;
    }
  }();

  if (!c) {
    return;
  }

  Err e = Err::OK;
  bool is_caller_done = false;
  {
    std::unique_lock l(c->mu_);
    if (!c->done_) {
      c->err_ = h.err_;
      if (c->err_ != OK) {
        std::cout << "RPCC: got pdu: RPC reply error for xid " << h.xid_ << ", err = " << ToString(c->err_)
                  << std::endl;
      }
      c->done_ = true;
      e = c->err_;
    } else {
      is_caller_done = true;
    }
  }

  if (!is_caller_done) {
    c->cb_(c->re_.ExtractReply(u), e);
  }
}

void RpcClient::ListenFromServer() {
  auto data = std::make_shared<std::string>(DEFAULT_RPC_SZ, ' ');
  socket_->Receive(*data, [me = shared_from_this(), data](size_t n, Err e) { me->OnReceiveFinished(data, n, e); });
}

void RpcClient::ConnectToServer() {
  socket_->Connect(host_, port_, [&](size_t, Err e) {
    if (e != Err::OK) {
      std::cout << "Err couldn't connect to server : " << ToString(e) << std::endl;
      throw std::runtime_error("cannot connect to server");
    }

    OnConnected();
  });

  workers_.reserve(num_worker_);
  for (int i = 0; i < num_worker_; i++) {
    workers_.push_back(std::thread([&] { ctx_->Run(); }));
  }
}
void RpcClient::OnConnected() {
  std::cout << "connected to server" << std::endl;
  {
    std::unique_lock l(connected_mu_);
    connected_ = true;
    connected_cv_.notify_all();
  }

  ListenFromServer();
}

void RpcClient::OnTimerExpired(RpcClient::Caller *c, Err e) {
  if (c == nullptr) {
    return;
  }

  if (e != Err::OK) {
    std::cout << "warning: err on callback of timer" << std::endl;
  }

  std::unique_lock l(c->mu_);
  if (!c->done_) {
    if (clt_nonce_ == 0) {
      std::cout << "Timeout waiting for reply for xid " << c->xid_ << std::endl;
      c->done_ = true;
      c->err_ = Err::TIMEOUT_FAILURE;
      c->cb_({}, c->err_);
    } else {
      std::cout << "Timeout waiting for reply for xid " << c->xid_ << ", retry ..." << std::endl;
      l.unlock();

      socket_->Send(c->buf_,
                    [me = shared_from_this(), xid = c->xid_](size_t n, Err e) { me->OnSendFinished(xid, n, e); });
    }
  }
}

void RpcClient::OnSendFinished(xid_t xid, size_t n, Err e) {
  if (e != Err::OK) {
    auto *c = [&]() mutable -> Caller * {
      std::unique_lock l(callers_mu_);
      if (callers_.contains(xid)) {
        return callers_[xid].get();
      }
      return nullptr;
    }();

    if (!c) {
      return;
    }

    // retry
    if (clt_nonce_ > 0) {
      if (e == BROKEN_PIPE) {
        socket_->Connect(host_, port_, [&, c](size_t, Err e) {
          if (e != Err::OK) {
            std::cout << "Err couldn't connect to server : " << ToString(e) << std::endl;
            throw std::runtime_error("cannot connect to server");
          }

          socket_->Send(c->buf_,
                        [me = shared_from_this(), xid = c->xid_](size_t n, Err e) { me->OnSendFinished(xid, n, e); });
        });
      } else {
        std::cout << "RPCC: " << clt_nonce_ << " error cannot send package to server for xid " << xid << ", retry ..."
                  << std::endl;
        socket_->Send(c->buf_,
                      [me = shared_from_this(), xid = c->xid_](size_t n, Err e) { me->OnSendFinished(xid, n, e); });
      }
    } else {
      {
        std::unique_lock l(c->mu_);
        c->done_ = true;
        c->err_ = e;
      }

      c->cb_({}, e);
    }
  } else {
//    std::cout << "RPCC: " << clt_nonce_ << ": send with xid " << xid << " successfully" << std::endl;
    ListenFromServer();
  }
}

void RpcClient::OnReceiveFinished(std::shared_ptr<std::string> data, size_t n, Err e) {
//  if (e == Err::CONNECTION_CLOSED) {
//    std::cout << "connection was closed by the server ..." << std::endl;
//    return;
//  }

  if (e == Err::CANCEL) {
    return;
  }

  if (e != Err::OK) {
    std::cout << "got an error " << ToString(e) << "while attempting to receive msg froms server";
    return;
  }

  data->resize(n);
  //  std::cout << "client rep buf: " << *data << std::endl;
  GotPdu(std::move(*data));
}

void RpcClient::UpdateXidRep(xid_t xid) {
  std::lock_guard l(rep_window_mu_);

  if (!xid_rep_window_.empty() && xid <= xid_rep_window_.front()) {
    return;
  }

  for (auto it = xid_rep_window_.begin(); it != xid_rep_window_.end(); it++) {
    if (*it > xid) {
      xid_rep_window_.insert(it, xid);
      goto compress;
    }
  }
  xid_rep_window_.push_back(xid);

compress:
  auto it = xid_rep_window_.begin();
  for (it++; it != xid_rep_window_.end(); it++) {
    while (xid_rep_window_.front() + 1 == *it) {
      xid_rep_window_.pop_front();
    }
  }
}

}  // namespace rpc
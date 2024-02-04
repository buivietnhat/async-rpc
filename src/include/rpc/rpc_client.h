#pragma once

#include "rpc/rpc.h"
#include "rpc/socket.h"

namespace rpc {

class RpcClient : public std::enable_shared_from_this<RpcClient> {
  using enum Err;
  using Reply = std::any;
  using Callback = std::function<void(Reply, Err)>;

  struct ReplyExtractor {
    std::function<std::any(Unmarshall &un)> extractor_;

    template <typename R>
    ReplyExtractor([[maybe_unused]] R) {
      extractor_ = [](Unmarshall &un) {
        R r{};
        un >> r;
        return r;
      };
    }

    std::any ExtractReply(Unmarshall &un) { return extractor_(un); }
  };

  class Caller {
    friend class RpcClient;

   public:
    Caller(xid_t xid, Callback &&cb, ReplyExtractor &&re);

    void SetBuf(std::string &&buf) { buf_ = std::move(buf); }

    std::string &Buf() { return buf_; }

   private:
    bool done_ = false;
    xid_t xid_ = -1;
    Err err_ = OK;
    Callback cb_;
    ReplyExtractor re_;
    mutex mu_;
    condition_variable cv_;

    std::string buf_;
  };

 public:
  RpcClient(std::unique_ptr<IoContext> ctx, std::unique_ptr<Socket> socket, std::string_view host,
            std::string_view port, int num_worker = RPC_DEFAULT_WORKERS, bool retrans = false);

  template <typename Args, typename Reply>
  Err Call(proc_t proc, const Args &args, Callback cb) {
    Marshall m;
    //    (m << ... << std::forward<Args>(args));
    m << args;

    Reply r;
    ReplyExtractor re(r);

    return Call(proc, std::move(m), std::move(cb), std::move(re));
  }

  void WaitUntilConnected() {
    std::unique_lock l(connected_mu_);
    if (!connected_cv_.wait_for(l, std::chrono::seconds(5), [&] { return connected_; })) {
      throw std::runtime_error("timeout waiting for connecting to server");
    }
  }

  ~RpcClient() {
    //    finished_ = true;

    socket_->Close();
    ctx_->Stop();
    for (auto &&w : workers_) {
      w.join();
    }
  }

 private:
  //  Err Call(proc_t proc, Marshall &&req, Callback &&cb, ReplyExtractor &&re) {
  //    auto err = Call(proc, std::move(req), std::move(cb), std::move(re));
  //    if (err != OK) {
  //      return err;
  //    }
  //
  //    //    u >> reply;
  //    return err;
  //  }

  Err Call(proc_t proc, Marshall &&req, Callback &&cb, ReplyExtractor &&re);

  void OnConnected() {
    std::cout << "connected to server" << std::endl;
    {
      std::unique_lock l(connected_mu_);
      connected_ = true;
      connected_cv_.notify_all();
    }

    ListenFromServer();
  }

  void OnSendFinished(xid_t xid, size_t n, Err e) {
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

      std::unique_lock l(c->mu_);
      c->done_ = true;
      c->err_ = e;
    }

    ListenFromServer();
  }

  void OnReceiveFinished(std::shared_ptr<std::string> data, size_t n, Err e) {
    if (e == Err::CONNECTION_CLOSED) {
      std::cout << "connection was closed by the server ..." << std::endl;
      //      socket->Close();
      return;
    }

    if (e != Err::OK) {
      std::cout << "got an error " << ToString(e) << "while attempting to receive msg froms server";
      return;
    }

    data->resize(n);
    std::cout << "client rep buf: " << *data << std::endl;
    GotPdu(std::move(*data));
  }

  void ListenFromServer();

  void GotPdu(std::string &&buf);

  Err Bind(TimeOut to = kToMax);

  mutable mutex callers_mu_;
  xid_t xid_ = 2;
  std::unordered_map<xid_t, std::unique_ptr<Caller>> callers_;

  uint32_t clt_nonce_ = 0;
  uint32_t srv_nonce_ = 0;
  //  [[maybe_unused]] std::list<xid_t> xid_rep_window_;

  std::unique_ptr<Socket> socket_;
  //  std::thread listen_thread_;

  std::unique_ptr<IoContext> ctx_;
  std::vector<std::thread> workers_;
  //  std::atomic<bool> finished_ = false;

  mutex connected_mu_;
  std::condition_variable connected_cv_;
  bool connected_ = false;
};

}  // namespace rpc
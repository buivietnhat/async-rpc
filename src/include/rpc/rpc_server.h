#pragma once

#include "common/fiber_thread_manager.h"
#include "common/fiber_thread_pool.h"
#include "rpc/rpc.h"
#include "rpc/socket.h"

namespace rpc {

using Handler = std::function<Err(Unmarshall &, Marshall &)>;

class RpcServer : public std::enable_shared_from_this<RpcServer> {
  using enum Err;

 public:
  explicit RpcServer(std::unique_ptr<Acceptor> acceptor, std::unique_ptr<IoContext> ctx,
                     int num_worker = DEFAULT_NUM_WORKER);

  ~RpcServer() { Stop(); }

  void Start();

  void AcceptNewConnection();

  void Stop() {
    if (!stoped_) {
      stoped_ = true;

      acceptor_->Close();
      ctx_->Stop();
      CloseAllClientSockets();

      for (auto &&w : workers_) {
        if (w.joinable()) {
          w.join();
        }
      }
    }
  }

  template <typename S, typename R, typename... Args>
  void Register(proc_t proc, S *sob, Err (S::*Method)(R &r, Args &&...args)) {
    auto handler = [sob, Method](Unmarshall &req, Marshall &rep) -> Err {
      std::tuple<Args...> args;
      (req >> ... >> std::get<Args>(args));

      R r;
      auto err = (sob->*Method)(r, std::get<Args>(args)...);
      rep << r;
      return err;
    };

    Register(proc, std::move(handler));
  }

 private:
  inline void CloseAllClientSockets() {
    std::lock_guard l(clnt_socket_mu_);
    for (auto &&s : client_sockets_) {
      s->Close();
    }
  }

  void ReadIncomingPacket(std::shared_ptr<Socket> socket);

  void Register(proc_t proc, Handler &&handler);

  void Dispatch(std::shared_ptr<Socket> socket, std::string &&buf);

  void OnNewConnection(std::unique_ptr<Socket> socket, Err e) {
    if (e != OK) {
      std::cout << "got an error " << ToString(e) << " while attempting to accept new connection" << std::endl;
      return;
    }

    {
      std::lock_guard l(clnt_socket_mu_);
      client_sockets_.push_back(socket.get());
    }

    ReadIncomingPacket(std::move(socket));
    AcceptNewConnection();
  }

  void OnSendFinished(std::shared_ptr<Socket> socket, std::shared_ptr<Marshall> rep, size_t n, Err e) {
    if (e != Err::OK) {
      std::cout << "got an error " << ToString(e) << " while attempting to send message" << std::endl;
      return;
    }

    ReadIncomingPacket(socket);
  }

  void OnReceiveFinished(std::shared_ptr<Socket> socket, std::shared_ptr<std::string> data, size_t n, Err e) {
    if (e == Err::CONNECTION_CLOSED) {
      std::cout << "connection was closed by the client ..." << std::endl;
      return;
    }

    if (e != Err::OK) {
      std::cout << "got an error " << ToString(e) << " while attempting to recive message" << std::endl;
      return;
    }

    data->resize(n);
    Dispatch(socket, std::move(*data));
  }

  uint32_t nonce_;

  mutex procs_mu_;
  std::unordered_map<xid_t, Handler> procs_;

  std::unique_ptr<IoContext> ctx_;
  std::unique_ptr<Acceptor> acceptor_;

  mutex clnt_socket_mu_;
  std::vector<Socket *> client_sockets_;
  bool stoped_ = false;
  int num_worker_;
  std::vector<std::thread> workers_;
  static constexpr int DEFAULT_NUM_WORKER = 4;
};

}  // namespace rpc

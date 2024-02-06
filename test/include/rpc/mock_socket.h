
#ifndef ASYNC_RPC_TEST_INCLUDE_RPC_MOCK_SOCKET_H_
#define ASYNC_RPC_TEST_INCLUDE_RPC_MOCK_SOCKET_H_

#include "rpc/socket.h"

namespace rpc {

class MockSocket;

static std::unordered_map<std::string, int> kSocks;
static std::unordered_map<int, std::string> kBuf;
static std::mutex kBufferMu;
static std::condition_variable kBufferCv;

static std::mutex kConnectionMu;
static std::condition_variable kConnectionCv;
static std::unordered_map<std::string, MockSocket *> kConnections;

class MockSocket : public Socket {
  friend class MockAcceptor;

 public:
  MockSocket() {
    static int id = 0;
    id_ = id++;
  }

  ~MockSocket() { Close(); }

  void Send(const std::string &buf, Callback callback) override {
    if (target_id_ == -1) {
      throw std::runtime_error("haven't connected to the target yet");
    }

    std::thread([&, cb = std::move(callback), buf] {
      std::unique_lock l(kBufferMu);
      kBuf[target_id_] = buf;
      kBufferCv.notify_all();
      cb(buf.size(), Err::OK);
    }).detach();
  }

  void Receive(std::string &buf, Callback callback) override {
    std::thread([&, cb = std::move(callback)] {
      std::unique_lock l(kBufferMu);
      kBufferCv.wait(l, [&] { return kBuf.contains(id_) || finished_; });

      Err e = Err::OK;
      if (finished_) {
        e = Err::CONNECTION_CLOSED;
      }

      buf = std::move(kBuf[id_]);

      kBuf.erase(id_);

      cb(buf.size(), e);
    }).detach();
  }

  void Connect(std::string_view host, std::string_view port, Callback cb) override {
    std::string addr = std::string(host) + ":" + std::string(port);
    {
      std::unique_lock l(kBufferMu);
      if (!kSocks.contains(addr)) {
        std::cerr << "no server listening on " << host << ":" << port << std::endl;
        return;
      }
    }

    {
      std::unique_lock l(kConnectionMu);
      target_id_ = kSocks[addr];
      kConnections[addr] = this;
      kConnectionCv.notify_all();
    }
  }

  void Close() override {
    if (closed_) {
      return;
    }

    closed_ = true;
    finished_ = true;
    kBufferCv.notify_all();
  }

 private:
  void Register(const std::string &addr) { kSocks[addr] = id_; }

  int id_ = -1;
  int target_id_ = -1;
  bool finished_ = false;
  bool closed_ = false;
};

class MockAcceptor : public Acceptor {
 public:
  MockAcceptor(std::string_view host, std::string_view port) { addr_ = std::string(host) + ":" + std::string(port); }

  void Accept(AcceptCallback callback) override {
    auto socket = std::make_unique<MockSocket>();
    socket->Register(addr_);

    std::thread([&, soc = std::move(socket), cb = std::move(callback)]() mutable {
      Err e = Err::OK;
      std::unique_lock l(kConnectionMu);
      kConnectionCv.wait(l, [&] { return (kConnections[addr_] != nullptr) || finished_; });
      if (finished_) {
        e = Err::CONNECTION_CLOSED;
      } else {
        soc->target_id_ = kConnections[addr_]->id_;
        kConnections.erase(addr_);
      }

      cb(std::move(soc), e);
    }).detach();

    return;
  }

  void Close() override {
    if (!stopped_) {
      stopped_ = true;
      finished_ = true;
      kConnectionCv.notify_all();
    }
  }

  ~MockAcceptor() { Close(); }

 private:
  bool finished_ = false;
  bool stopped_ = false;
  std::string addr_;
};

class MockIOContext : public IoContext {
 public:
  void Run() override {
    std::unique_lock l(mu_);
    cv_.wait(l, [&] { return finished_; });
  }

  void Stop() override {
    std::unique_lock l(mu_);
    finished_ = true;
    cv_.notify_all();
  }

  std::unique_ptr<Timer> CreateTimer(int miliseconds) override {
    throw std::runtime_error("not implemented");
  }

 private:
  bool finished_ = false;
  std::mutex mu_;
  std::condition_variable cv_;
};

}  // namespace rpc

#endif  // ASYNC_RPC_TEST_INCLUDE_RPC_MOCK_SOCKET_H_

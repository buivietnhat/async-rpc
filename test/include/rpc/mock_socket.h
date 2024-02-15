
#ifndef ASYNC_RPC_TEST_INCLUDE_RPC_MOCK_SOCKET_H_
#define ASYNC_RPC_TEST_INCLUDE_RPC_MOCK_SOCKET_H_

#include "rpc/socket.h"

namespace rpc {

class MockSocket;

static std::unordered_map<std::string, int> kSocks;
static std::unordered_map<int, std::deque<std::string>> kBuf;
static std::mutex kBufferMu;
static std::condition_variable kBufferCv;

static std::mutex kConnectionMu;
static std::condition_variable kConnectionCv;
static std::unordered_map<std::string, MockSocket *> kConnections;
static std::atomic<bool> kFinished = false;

class MockSocket : public Socket {
  friend class MockAcceptor;

 public:
  MockSocket(bool lossy_test = 0) : lossy_(lossy_test) {
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
      if (lossy_ > 0) {
        auto rand = random();
        if (rand % 100 < lossy_) {
          // drop the package
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          std::cout << "Mock socket: Dropping the sending package" << std::endl;
          if (random() % 2 > 0) {
            // still send the package but notify that the ops was failed
            std::cout << "Mock socket: But still send it through" << std::endl;
            kBuf[target_id_].push_back(buf);
            kBufferCv.notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
          }
          cb({}, Err::SEND_FAILURE);
        } else {
          kBuf[target_id_].push_back(buf);
          kBufferCv.notify_all();
          cb(buf.size(), Err::OK);
        }
      } else {
        kBuf[target_id_].push_back(buf);
        kBufferCv.notify_all();
        cb(buf.size(), Err::OK);
      }
    }).detach();
  }

  void Receive(std::string &buf, Callback callback) override {
    std::thread([&, cb = std::move(callback)] {
      std::unique_lock l(kBufferMu);
      kBufferCv.wait(l, [&] { return (kBuf.contains(id_) && !kBuf[id_].empty()) || kFinished; });

      Err e = Err::OK;
      if (kFinished) {
        e = Err::CANCEL;
        cb({}, e);
        return;
      }

      buf = std::move(kBuf[id_].front());
      kBuf[id_].pop_front();

      if (kBuf[id_].empty()) {
        kBuf.erase(id_);
      }

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
    cb(0, Err::OK);
  }

  void Close() override {
    if (closed_) {
      return;
    }

    closed_ = true;
    kFinished = true;
    kBufferCv.notify_all();
  }

 private:
  void Register(const std::string &addr) { kSocks[addr] = id_; }

  int id_ = -1;
  int target_id_ = -1;
  bool closed_ = false;
  int lossy_ = 10;
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
      kConnectionCv.wait(l, [&] { return (kConnections[addr_] != nullptr) || kFinished; });
      if (kFinished) {
        e = Err::CANCEL;
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
      kFinished = true;
      kConnectionCv.notify_all();
    }
  }

  ~MockAcceptor() { Close(); }

 private:
  bool stopped_ = false;
  std::string addr_;
};

class MockTimer : public Timer {
 public:
  explicit MockTimer(int timeout_milis) : timeout_milis_(timeout_milis) {
    cancel_ = std::make_shared<std::atomic<bool>>(false);
  }

  void Wait(TimerCallback cb) override {
    std::thread([cb = std::move(cb), to = timeout_milis_, canceled = cancel_] {
      std::this_thread::sleep_for(std::chrono::milliseconds(to));
      if (!*canceled) {
        cb(Err::OK);
      }
    }).detach();
  }

  ~MockTimer() { *cancel_ = true; }

 private:
  int timeout_milis_;
  std::shared_ptr<std::atomic<bool>> cancel_;
};

class MockIOContext : public IoContext {
 public:
  void Run() override {
    std::unique_lock l(mu_);
    cv_.wait(l, [&] { return kFinished == true; });
  }

  void Stop() override {
    std::unique_lock l(mu_);
    kFinished = true;
    cv_.notify_all();
  }

  std::unique_ptr<Timer> CreateTimer(int miliseconds) override { return std::make_unique<MockTimer>(miliseconds); }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
};

}  // namespace rpc

#endif  // ASYNC_RPC_TEST_INCLUDE_RPC_MOCK_SOCKET_H_

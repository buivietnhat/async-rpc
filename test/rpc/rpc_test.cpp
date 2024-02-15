#include "gtest/gtest.h"
#include "rpc/marshall.h"
#include "rpc/mock_socket.h"
#include "rpc/rpc_client.h"
#include "rpc/rpc_server.h"

using namespace rpc;
using namespace std::chrono_literals;

TEST(RPCTest, DISABLED_Marshall) {
  Marshall m;
  int x = 2;
  uint64_t y = 4;
  std::string s = "hello";
  m << x << y << s;

  Unmarshall u(m.Buf());
  int x1;
  uint64_t y1;
  std::string s1;

  u >> x1 >> y1 >> s1;

  EXPECT_EQ(x, x1);
  EXPECT_EQ(y, y1);
  EXPECT_EQ(s, s1);

  m.Reset();

  ReplyHeader rh{2, Err::OK};
  m << rh;

  u.TakeBuf(m.Buf());
  ReplyHeader rh1;
  u >> rh1;

  EXPECT_EQ(rh, rh1);
}

TEST(RPCTest, DISABLED_TestHandler) {
  std::string_view host = "0.0.0.0";
  std::string_view server_port = "2000";

  auto client_socket = std::make_unique<MockSocket>();
  auto server_socket = std::make_unique<MockSocket>();

  auto acceptor = std::make_unique<MockAcceptor>(host, server_port);

  auto clt_ctx = std::make_unique<MockIOContext>();
  auto srv_ctx = std::make_unique<MockIOContext>();

  auto server = std::make_shared<RpcServer>(std::move(acceptor), std::move(srv_ctx), 2);
  server->AcceptNewConnection();

  auto client = std::make_shared<RpcClient>(std::move(clt_ctx), std::move(client_socket), host, server_port, 2);
  client->ConnectToServer();
  client->WaitUntilConnected();

  class Foo {
   public:
    Foo(int x, int y) : x_(x), y_(y) {}

    Err GetX(const int &a, int &x) {
      std::cout << "GetX called" << std::endl;
      x = x_;
      return Err::OK;
    }

   private:
    int x_, y_;
  };

  int x = 1, y = 2;
  proc_t GETX = 1;

  Foo foo{x, y};
  server->Register(GETX, &foo, &Foo::GetX);

  std::mutex mu;
  std::condition_variable cv;
  bool got_reply = false;

  int rep_x;
  auto callback = [&](std::any reply, Err e) {
    EXPECT_EQ(e, Err::OK);
    try {
      rep_x = std::any_cast<int>(reply);
    } catch (std::bad_any_cast &e) {
      ASSERT_TRUE(false);
    }

    EXPECT_EQ(x, rep_x);

    std::unique_lock l(mu);
    got_reply = true;
    cv.notify_all();
  };

  using GetXArgs = int;
  GetXArgs args;

  client->Call<GetXArgs, int>(GETX, args, std::move(callback));

  std::unique_lock l(mu);
  cv.wait(l, [&] { return got_reply; });
  server->Stop();
}

TEST(RPCTest, DISABLED_TestHandlerWithBoostAsio) {
  std::string_view host = "0.0.0.0";
  std::string_view server_port = "2000";

  auto asio_srv_ctx = std::make_unique<boost::asio::io_context>();
  auto asio_clt_ctx = std::make_unique<boost::asio::io_context>();

  auto asio_srv_ctx_ptr = asio_srv_ctx.get();
  auto asio_clt_ctx_ptr = asio_clt_ctx.get();

  auto clt_ctx = std::make_unique<AsioIoContext>(std::move(asio_clt_ctx));
  auto srv_ctx = std::make_unique<AsioIoContext>(std::move(asio_srv_ctx));

  auto client_socket = std::make_unique<AsyncSocket>(asio_clt_ctx_ptr);

  auto acceptor = std::make_unique<AsyncAcceptor>(asio_srv_ctx_ptr, host, server_port);
  auto server = std::make_shared<RpcServer>(std::move(acceptor), std::move(srv_ctx), 2);
  server->Start();

  auto client = std::make_shared<RpcClient>(std::move(clt_ctx), std::move(client_socket), host, server_port, 2);
  client->ConnectToServer();
  client->WaitUntilConnected();

  class Foo {
   public:
    Foo(int x, int y) : x_(x), y_(y) {}

    Err GetX(const int &a, int &x) {
      std::cout << "GetX called" << std::endl;
      x = x_;
      return Err::OK;
    }

    Err GetY(int &y) const {
      y = y_;
      return Err::OK;
    }

   private:
    int x_, y_;
  };

  int x = 1, y = 2;
  proc_t GETX = 1;

  Foo foo{x, y};
  server->Register(GETX, &foo, &Foo::GetX);

  std::mutex mu;
  std::condition_variable cv;
  bool got_reply = false;

  int rep_x;
  auto callback = [&](std::any reply, Err e) {
    EXPECT_EQ(e, Err::OK);
    try {
      rep_x = std::any_cast<int>(reply);
    } catch (std::bad_any_cast &e) {
      ASSERT_TRUE(false);
    }

    EXPECT_EQ(x, rep_x);

    std::unique_lock l(mu);
    got_reply = true;
    cv.notify_all();
  };

  using GetXArgs = int;
  GetXArgs args;

  client->Call<GetXArgs, int>(GETX, args, std::move(callback));

  std::unique_lock l(mu);
  cv.wait(l, [&] { return got_reply; });
  server->Stop();
}

TEST(RPCTest, DISABLED_TestTimeout) {
  std::string_view host = "0.0.0.0";
  std::string_view server_port = "2000";

  auto asio_srv_ctx = std::make_unique<boost::asio::io_context>();
  auto asio_clt_ctx = std::make_unique<boost::asio::io_context>();

  auto asio_srv_ctx_ptr = asio_srv_ctx.get();
  auto asio_clt_ctx_ptr = asio_clt_ctx.get();

  auto clt_ctx = std::make_unique<AsioIoContext>(std::move(asio_clt_ctx));
  auto srv_ctx = std::make_unique<AsioIoContext>(std::move(asio_srv_ctx));

  auto client_socket = std::make_unique<AsyncSocket>(asio_clt_ctx_ptr);

  auto acceptor = std::make_unique<AsyncAcceptor>(asio_srv_ctx_ptr, host, server_port);
  auto server = std::make_shared<RpcServer>(std::move(acceptor), std::move(srv_ctx), 2);
  server->Start();

  auto client = std::make_shared<RpcClient>(std::move(clt_ctx), std::move(client_socket), host, server_port, 2);
  client->ConnectToServer();
  client->WaitUntilConnected();

  class Foo {
   public:
    Foo(int x, int y) : x_(x), y_(y) {}

    Err GetX(const int &a, int &x) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      std::cout << "GetX called" << std::endl;
      x = x_;
      return Err::OK;
    }

    Err GetY(int &y) const {
      y = y_;
      return Err::OK;
    }

   private:
    int x_, y_;
  };

  int x = 1, y = 2;
  proc_t GETX = 1;

  Foo foo{x, y};
  server->Register(GETX, &foo, &Foo::GetX);

  std::mutex mu;
  std::condition_variable cv;
  bool got_reply = false;

  auto callback = [&](std::any reply, Err e) {
    EXPECT_EQ(e, Err::TIMEOUT_FAILURE);

    std::unique_lock l(mu);
    got_reply = true;
    cv.notify_all();
  };

  using GetXArgs = int;
  GetXArgs args;

  TimeOut to{500};
  client->Call<GetXArgs, int>(GETX, args, std::move(callback), to);

  std::unique_lock l(mu);
  cv.wait(l, [&] { return got_reply; });
  server->Stop();
}

TEST(RPCTest, LossyTest) {
  std::string_view host = "0.0.0.0";
  std::string_view server_port = "2000";

  auto server_socket = std::make_unique<MockSocket>();
  auto acceptor = std::make_unique<MockAcceptor>(host, server_port);
  auto srv_ctx = std::make_unique<MockIOContext>();
  auto server = std::make_shared<RpcServer>(std::move(acceptor), std::move(srv_ctx), 2);
  server->Start();

  class Server {
   public:
    Err Handler(const int &len, std::string &r) {
      r = std::string(len, 'x');
      return Err::OK;
    }
  };

  static constexpr int NUM_CLIENTS = 2;
  const int lossy_test = 5;
  std::vector<std::shared_ptr<RpcClient>> clients;
  for (int i = 0; i < NUM_CLIENTS; i++) {
    auto client_socket = std::make_unique<MockSocket>(lossy_test);
    auto clt_ctx = std::make_unique<MockIOContext>();
    auto client = std::make_shared<RpcClient>(std::move(clt_ctx), std::move(client_socket), host, server_port, 2,
                                              true);
    clients.push_back(client);
    client->ConnectToServer();
    client->WaitUntilConnected();
    client->Bind(TimeOut{3000});
  }

  const int num_request = 200;
  const proc_t PROC = 200;
  Server s;
  server->Register(PROC, &s, &Server::Handler);
  std::atomic<int> received_count = 0;

  auto func = [&](unsigned long xx) {
    int which_cl = xx % NUM_CLIENTS;

    // wait for the client to bind
    while (!clients[which_cl]->HasBinded()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    for (int i = 0; i < num_request; i++) {
      int arg = (random() % 2000);
      auto e = clients[which_cl]->Call<int, std::string>(PROC, arg, [&, arg](std::any r, Err e) {
        auto rep = std::any_cast<std::string>(r);
        ASSERT_EQ(static_cast<int>(rep.size()), arg);
        received_count += 1;
        std::cout << "received count is now " << received_count << std::endl;
      }, TimeOut{3000});
      ASSERT_EQ(e, Err::OK);
    }
  };

  const int num_thread = 5;
  std::vector<std::thread> threads;
  for (int i = 0; i < num_thread; i++) {
    threads.push_back(std::thread(func, i));
  }

  while (received_count < num_request * num_thread) {}

  for (auto &&t : threads) {
    t.join();
  }

  server->Stop();
}
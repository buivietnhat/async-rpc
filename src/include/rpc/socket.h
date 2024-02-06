
#pragma once

// #define BOOST_ASIO_ENABLE_HANDLER_TRACKING 1

#include <array>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <iostream>
#include <memory>
#include <vector>

#include "rpc/rpc.h"

namespace rpc {

class Socket {
 public:
  using Callback = std::function<void(size_t, Err e)>;

  virtual void Send(const std::string &buf, Callback callback) = 0;
  virtual void Receive(std::string &buf, Callback callback) = 0;
  virtual void Connect(std::string_view host, std::string_view port, Callback callback) = 0;
  virtual void Close() = 0;

  virtual ~Socket() = default;
};

class Acceptor {
 public:
  using AcceptCallback = std::function<void(std::unique_ptr<Socket>, Err)>;

  virtual void Accept(AcceptCallback callback) = 0;
  virtual ~Acceptor() = default;
  virtual void Close() = 0;
};

class Timer {
 public:
  using TimerCallback = std::function<void(Err)>;
  virtual void Wait(TimerCallback cb) = 0;
  virtual ~Timer() = default;
};

class IoContext {
 public:
  virtual void Run() = 0;
  virtual void Stop() = 0;
  virtual std::unique_ptr<Timer> CreateTimer(int miliseconds) = 0;
  virtual ~IoContext() = default;
};

class AsyncSocket : public Socket {
 public:
  explicit AsyncSocket(boost::asio::io_context *ctx) : ctx_(ctx) {
    if (ctx_ == nullptr) {
      throw std::runtime_error("async socket: passing null ctx");
    }

    socket_ = std::make_unique<boost::asio::ip::tcp::socket>(*ctx);
  }

  AsyncSocket(boost::asio::io_context *ctx, std::unique_ptr<boost::asio::ip::tcp::socket> socket)
      : ctx_(ctx), socket_(std::move(socket)) {}

  void Send(const std::string &buf, Callback callback) override {
    socket_->async_send(boost::asio::buffer(buf),
                        [cb = std::move(callback)](boost::system::error_code ec, size_t length) {
                          Err e = Err::OK;
                          if (ec) {
                            e = Err::SEND_FAILURE;
                          }

                          cb(length, e);
                        });
  }

  void Close() override {
    if (closed_) {
      return;
    }

    closed_ = true;
    try {
      socket_->close();
    } catch (...) {
      std::cerr << "couldn't close socket" << std::endl;
    }
  }

  void Receive(std::string &buf, Callback callback) override {
    socket_->async_read_some(boost::asio::buffer(buf),
                             [cb = std::move(callback)](boost::system::error_code ec, size_t length) {
                               Err e = Err::OK;
                               if (ec == boost::asio::error::eof) {
                                 // connection closed cleanly by peer
                                 e = Err::CONNECTION_CLOSED;
                               } else if (ec) {
                                 e = Err::RECEIVE_FAILURE;
                               }
                               cb(length, e);
                             });
  }

  void Connect(std::string_view host, std::string_view port, Callback callback) override {
    auto endpoint = *boost::asio::ip::tcp::resolver(*ctx_).resolve(host, port);
    socket_->async_connect(endpoint, [cb = std::move(callback)](std::error_code ec) {
      Err e = Err::OK;
      if (ec) {
        e = Err::CONNECT_FAILURE;
      }
      cb(0, e);
    });
  }

  ~AsyncSocket() { Close(); }

 private:
  bool closed_ = false;
  boost::asio::io_context *ctx_;
  std::unique_ptr<boost::asio::ip::tcp::socket> socket_;
};

class AsyncAcceptor : public Acceptor {
 public:
  AsyncAcceptor(boost::asio::io_context *ctx, std::string_view host, std::string_view port) : ctx_(ctx) {
    if (ctx == nullptr) {
      throw std::runtime_error("passing null ctx for AsyncAcceptor constructor");
    }

    auto listen_endpoint =
        *boost::asio::ip::tcp::resolver(*ctx).resolve(host, port, boost::asio::ip::tcp::resolver::passive);
    acceptor_ = std::make_unique<boost::asio::ip::tcp::acceptor>(*ctx, listen_endpoint);
  }

  void Close() override {
    acceptor_->cancel();
    acceptor_->close();
  }

  void Accept(AcceptCallback callback) override {
    auto socket = std::make_unique<boost::asio::ip::tcp::socket>(*ctx_);
    acceptor_->async_accept(
        *socket, [cb = std::move(callback), sk = std::move(socket), ctx = ctx_](boost::system::error_code ec) mutable {
          Err e = Err::OK;
          if (ec) {
            e = Err::ACCEPT_FAILURE;
          }
          cb(std::make_unique<AsyncSocket>(ctx, std::move(sk)), e);
        });
  }

 private:
  boost::asio::io_context *ctx_;
  std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
};

class AsioTimer : public Timer {
 public:
  AsioTimer(boost::asio::io_context *ctx, int miliseconds) {
    if (ctx == nullptr) {
      throw std::runtime_error("asio timer: null ctx for constructor");
    }

    timer_ = std::make_unique<boost::asio::deadline_timer>(*ctx, boost::posix_time::milliseconds(miliseconds));
  }

  void Wait(TimerCallback cb) override {
    timer_->async_wait([cb = std::move(cb)](const boost::system::error_code &error) {

      if (error == boost::asio::error::operation_aborted) {
        std::cout << "timer: operation aborted" << std::endl;
        return;
      }

      Err e = Err::OK;
      if (error) {
        std::cout << "warning: error on callback for boost asio timer " << error.message() << std::endl;
        e = Err::TIMER_FAILURE;
      }

      cb(e);
    });
  }

 private:
  std::unique_ptr<boost::asio::deadline_timer> timer_;
};

class AsioIoContext : public IoContext {
 public:
  AsioIoContext(std::unique_ptr<boost::asio::io_context> ctx) : ctx_(std::move(ctx)) {
    if (!ctx_) {
      throw std::runtime_error("null ctx for AsioIoContext");
    }
  }
  void Run() override { ctx_->run(); }

  void Stop() override { ctx_->stop(); }

  std::unique_ptr<Timer> CreateTimer(int miliseconds) override {
    return std::make_unique<AsioTimer>(ctx_.get(), miliseconds);
  }

 private:
  std::unique_ptr<boost::asio::io_context> ctx_;
};

}  // namespace rpc

#pragma once

#include <cassert>
#include <concepts>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace rpc {

static constexpr int DEFAULT_RPC_SZ = 1024;

struct RequestHeader;
struct ReplyHeader;

template <typename T>
concept ToJsonSupport = requires(const T &t, nlohmann::json &j) { to_json(j, t); };

template <typename T>
concept FromJsonSupport = requires(T &t, const nlohmann::json &j) { from_json(j, t); };

template <typename T>
concept JsonSupport = ToJsonSupport<T> && FromJsonSupport<T>;

class Marshall {
 public:
  Marshall() = default;

  void Reset() { json_.clear(); }

  inline std::string Buf() const { return json_.dump(); }

  template <typename T>
    requires JsonSupport<T>
  Marshall &operator<<(const T &t) {
    json_[std::to_string(id_++)] = t;
    return *this;
  }

  Marshall &operator<<(const RequestHeader &r);
  Marshall &operator<<(const ReplyHeader &r);

  void Print() const {
    for (const auto &el : json_.items()) {
      std::cout << el.key() << " : " << el.value() << "\n";
    }
  }

 private:
  nlohmann::json json_;
  int id_ = 0;
};

class Unmarshall {
 public:
  Unmarshall() = default;

  Unmarshall(const std::string &buf) { TakeBuf(buf); }

  void TakeBuf(const std::string &buf) {
    try {
      json_ = nlohmann::json::parse(buf);
    } catch (const std::exception &e) {
      std::cerr << "couldn't parse the input buf for marshalling" << std::endl;
      throw e;
    }
  }

  void Print() const {
    for (const auto &el : json_.items()) {
      std::cout << el.key() << " : " << el.value() << "\n";
    }
  }

  template <typename T>
    requires JsonSupport<T>
  Unmarshall &operator>>(T &t) {
    json_.at(std::to_string(id_++)).get_to(t);
    return *this;
  }

  Unmarshall &operator>>(RequestHeader &r);
  Unmarshall &operator>>(ReplyHeader &r);

 private:
  int id_ = 0;
  nlohmann::json json_;
};

}  // namespace rpc
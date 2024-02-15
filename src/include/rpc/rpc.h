#pragma once

#include <boost/asio.hpp>
#include <boost/fiber/all.hpp>
#include <cstddef>
#include <list>
#include <memory>
#include <unordered_map>

#include "marshall.h"

namespace rpc {

using proc_t = size_t;
using xid_t = size_t;

using mutex = std::mutex;
using condition_variable = std::condition_variable;

static constexpr int RPC_DEFAULT_WORKERS = 2;

enum class Err : uint8_t {
  RESERVED,
  OK,
  UNMARSHALL_REPLY_FAILURE,
  UNMARSHALL_ARGS_FAILURE,
  BIND_FAILURE,
  ATMOST_ONCE_FAILURE,
  TIMEOUT_FAILURE,
  OLDSERVER_FAILURE,
  SEND_FAILURE,
  RECEIVE_FAILURE,
  CONNECTION_CLOSED,
  ACCEPT_FAILURE,
  CONNECT_FAILURE,
  CANCEL,
  TIMER_FAILURE,
  BROKEN_PIPE,
  CONNECTION_RESET,
  CONNECTION_ABORTED,
};

inline std::string ToString(Err err) {
  switch (err) {
    case Err::RESERVED:
      return "RESERVED";
    case Err::OK:
      return "OK";
    case Err::UNMARSHALL_REPLY_FAILURE:
      return "UNMARSHALL_REPLY_FAILURE";
    case Err::BIND_FAILURE:
      return "BIND_FAILURE";
    case Err::ATMOST_ONCE_FAILURE:
      return "ATMOST_ONCE_FAILURE";
    case Err::TIMEOUT_FAILURE:
      return "TIMEOUT_FAILURE";
    case Err::SEND_FAILURE:
      return "TIMEOUT_FAILURE";
    case Err::RECEIVE_FAILURE:
      return "RECEIVE_FAILURE";
    case Err::CONNECTION_CLOSED:
      return "CONNECTION_CLOSED";
    case Err::ACCEPT_FAILURE:
      return "ACCEPT_FAILURE";
    case Err::UNMARSHALL_ARGS_FAILURE:
      return "UNMARSHALL_ARGS_FAILURE";
    case Err::OLDSERVER_FAILURE:
      return "OLDSERVER_FAILURE";
    case Err::CONNECT_FAILURE:
      return "CONNECT_FAILURE";
    case Err::CANCEL:
      return "CANCEL";
    case Err::TIMER_FAILURE:
      return "TIMER_FAILURE";
    case Err::BROKEN_PIPE:
      return "BROKEN_PIPE";
    case Err::CONNECTION_RESET:
      return "CONNECTION_RESET";
    case Err::CONNECTION_ABORTED:
      return "CONNECTION_ABORTED";
    default:
      return "";
  }
}

struct TimeOut {
  int ms_;
};

static constexpr TimeOut kToMax = {12000};
static constexpr TimeOut kToMin = {1000};

static constexpr xid_t BIND = 1000;

struct RequestHeader {
  xid_t xid_;
  proc_t proc_;
  uint32_t clt_nonce_;
  uint32_t srv_nonce_;
  xid_t xid_rep_;

  RequestHeader(xid_t xid = 0, proc_t proc = 0, uint32_t clt_nonce = 0, uint32_t srv_nonce = 0, xid_t xid_rep = 0)
      : xid_(xid), proc_(proc), clt_nonce_(clt_nonce), srv_nonce_(srv_nonce), xid_rep_(xid_rep) {}
};

struct ReplyHeader {
  xid_t xid_;
  Err err_;

  ReplyHeader(xid_t xid = 0, Err err = Err::OK) : xid_(xid), err_(err) {}

  bool operator==(const ReplyHeader &rhs) const { return xid_ == rhs.xid_ && err_ == rhs.err_; }
  bool operator!=(const ReplyHeader &rhs) const { return !(rhs == *this); }

  friend std::ostream &operator<<(std::ostream &o, const ReplyHeader &r) {
    o << "xid : " << r.xid_ << ", err: " << ToString(r.err_);
    return o;
  }
};

inline void to_json(nlohmann::json &j, const RequestHeader &req) {
  j = nlohmann::json{{"xid", req.xid_},
                     {"proc", req.proc_},
                     {"clt_nonce", req.clt_nonce_},
                     {"srv_nonce", req.srv_nonce_},
                     {"xid_rep", req.xid_rep_}};
}

inline void from_json(const nlohmann::json &j, RequestHeader &req) {
  j.at("xid").get_to(req.xid_);
  j.at("proc").get_to(req.proc_);
  j.at("clt_nonce").get_to(req.clt_nonce_);
  j.at("srv_nonce").get_to(req.srv_nonce_);
  j.at("xid_rep").get_to(req.xid_rep_);
}

inline void to_json(nlohmann::json &j, const ReplyHeader &rep) {
  j = nlohmann::json{{"xid", rep.xid_}, {"err", rep.err_}};
}

inline void from_json(const nlohmann::json &j, ReplyHeader &rep) {
  j.at("xid").get_to(rep.xid_);
  j.at("err").get_to(rep.err_);
}

}  // namespace rpc
#include "rpc/marshall.h"

#include "rpc/rpc.h"

namespace rpc {

Marshall &Marshall::operator<<(const RequestHeader &r) {
  json_["req"] = r;
  return *this;
}

Marshall &Marshall::operator<<(const ReplyHeader &r) {
  json_["rep"] = r;
  return *this;
}

Unmarshall &Unmarshall::operator>>(RequestHeader &r) {
  json_.at("req").get_to(r);
  return *this;
}

Unmarshall &Unmarshall::operator>>(ReplyHeader &r) {
  json_.at("rep").get_to(r);
  return *this;
}

}  // namespace rpc
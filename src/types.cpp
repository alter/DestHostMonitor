#include "types.hpp"

namespace pt {

const char* to_string(Status s) {
    switch (s) {
        case Status::Ok:           return "OK";
        case Status::Timeout:      return "TIMEOUT";
        case Status::DestUnreach:  return "DEST_UNREACH";
        case Status::TtlExpired:   return "TTL_EXPIRED";
        case Status::OtherIcmpErr: return "OTHER_ICMP_ERR";
        case Status::SendErr:      return "SEND_ERR";
        case Status::MonitorGap:   return "MONITOR_GAP";
    }
    return "?";
}

}  // namespace pt

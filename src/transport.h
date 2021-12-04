#ifndef _RAFT_TRANSPORT_H_
#define _RAFT_TRANSPORT_H_

#include "proto/raftmsg.pb.h"

class RaftServer;
class RaftNode;
class address_t;
class dialer_t;
class service_t;
class engine_t;
class request_t;
class response_t;

class Transport {
public:
    Transport(engine_t *eng, RaftServer *rs);
    int Start(address_t *addr, service_t *svr);
    int Send(const address_t *addr, const std::shared_ptr<raft::RaftMessage> msg);
private:
    int Receive(response_t *rsp);

    engine_t *eng_;
    RaftServer *raft_server_;
    std::map<uint64_t, dialer_t *> clients_;
    std::map<const address_t *, service_t *> servers_;
};

#endif

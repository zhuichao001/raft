#ifndef _RAFT_TRANSPORT_H_
#define _RAFT_TRANSPORT_H_

#include "lotus/dialer.h"
#include "proto/raftmsg.pb.h"

class RaftServer;
class RaftNode;
class address_t;
class engine_t;
class rpc_request_t;
class rpc_response_t;

template<typename REQUEST, typename RESPONSE>
class service_t;

class Transport {
public:
    Transport(engine_t *eng, RaftServer *rs);
    int Send(const address_t *addr, const std::shared_ptr<raft::RaftMessage> msg);
private:
    int Receive(rpc_request_t *req, rpc_response_t *rsp);

    engine_t *eng_;
    RaftServer *raft_server_;
    std::map<uint64_t, std::shared_ptr<dialer_t<rpc_request_t, rpc_response_t>>> clients_;
    //std::map<const address_t *, service_t *> servers_;
};

#endif

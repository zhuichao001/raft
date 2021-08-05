#ifndef _RAFT_TRANSPORT_H_
#define _RAFT_TRANSPORT_H_

#include "proto/raftmsg.pb.h"

class RaftServer;
class RaftNode;
class address_t;
class dialer_t;
class server_t;
class engine_t;
class request_t;
class response_t;


class Transport {
public:
    Transport(engine_t *eng, RaftServer *rs);

    int Start(address_t *addr, server_t *svr);

    void Send(const address_t *addr, const raft::RaftMessage *msg);

private:
    int dispatch(request_t *req, response_t *rsp);

private:
    engine_t *eng_;
    RaftServer *raft_server_;
    std::map<const address_t *, dialer_t *> clients_;
    std::map<const address_t *, server_t *> servers_;
};

#endif

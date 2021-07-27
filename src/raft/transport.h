#ifndef _RAFT_TRANSPORT_H_
#define _RAFT_TRANSPORT_H_

#include <stdio.h>
#include "lotus/dialer.h"
#include "lotus/engine.h"
#include "lotus/server.h"
#include "proto/raftmsg.pb.h"

class RaftServer;
class RaftNode;

class Transport {
public:
    Transport(RaftServer *rs);

    int Start(address_t *addr, server_t *svr);

    void Stop();

    void Run();

    void Send(const RaftNode *to, const raft::RaftMessage *msg);

private:
    int dispatch(request_t *req, response_t *rsp);

private:
    RaftServer *raft_server_;
    std::map<const address_t *, dialer_t *> clients;
    std::map<const address_t *, server_t *> servers;
    engine_t eng;
};

#endif

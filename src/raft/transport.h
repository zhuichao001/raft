#ifndef _RAFT_TRANSPORT_H_
#define _RAFT_TRANSPORT_H_

#include <stdio.h>
#include "lotus/dialer.h"
#include "lotus/engine.h"
#include "lotus/server.h"
#include "proto/raftmsg.pb.h"

class Transport {
public:
    int Start(address_t *addr, server_t *svr){
        if (addr == nullptr){
            return -1;
        }
        servers[addr] = svr;
        eng.start(addr, svr);
        return 0;
    }

    void Stop() {
        eng.stop();
    }

    void Run(){
        eng.run();
    }

    void Send(address_t *addr, const raft::RaftMessage *msg){
        auto it = clients.find(addr);
        if (it==clients.end()){
            dialer_t *cli = eng.open(addr);
            clients[addr] = cli;
        }

        string tmp;
        msg->SerializeToString(&tmp);
        request_t req;
        req.setbody(tmp.c_str(), tmp.size());

        RpcCallback callback = std::bind(&Transport::dispatch, this, std::placeholders::_1, std::placeholders::_2);
        clients[addr]->call(&req, callback);
    }

private:
    int dispatch(request_t *req, response_t *rsp){
        raft::RaftMessage msg;
        msg.ParseFromString(rsp->data());
        Raft *raf =nullptr; //TODO
        switch(msg.type()){
            case raft::RaftMessage::MSGTYPE_APPENDLOG_RESPONSE:
                raf->recvAppendEntriesResponse(&msg.ae_rsp());
                break;
            case raft::RaftMessage::MSGTYPE_VOTE_RESPONSE:
                raf->recvVoteResponse(&msg.vt_rsp());
                break;
            case raft::RaftMessage::MSGTYPE_HANDSHAKE_RESPONSE:
                break;
            default:
                fprintf(stderr, "unknown msg type:%d\n", msg.type());
        }
        return 0;
    }

private:
    std::map<address_t *, dialer_t *> clients;
    std::map<address_t *, server_t *> servers;
    engine_t eng;
};

#endif

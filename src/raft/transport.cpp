#include <stdio.h>
#include "lotus/address.h"
#include "lotus/dialer.h"
#include "lotus/engine.h"
#include "lotus/server.h"
#include "raft_node.h"
#include "raft.h"
#include "raft_server.h"
#include "transport.h"

Transport::Transport(engine_t *eng, RaftServer *rs):
    eng_(eng),
    raft_server_(rs){
}

int Transport::Start(address_t *addr, server_t *svr){
    if (addr == nullptr){
        return -1;
    }
    servers_[addr] = svr;
    eng_->start(addr, svr);
    return 0;
}

int Transport::Send(const address_t *addr, const std::shared_ptr<raft::RaftMessage> msg){
    int64_t hip = addr->to_long();
    if(clients_.find(hip)==clients_.end()){
        dialer_t *cli = eng_->open(addr);
        if(cli!=nullptr){
            clients_[hip] = cli;
        } else {
            fprintf(stderr, "|||Send failed(not connect success). ip:%s port:%d\n", addr->ip.c_str(), addr->port);
            return -1;
        }
    }

    string tmp;
    msg->SerializeToString(&tmp);
    fprintf(stderr, "SEND:%X len=%d\n", tmp.c_str(), tmp.size());

    request_t req;
    req.setbody(tmp.c_str(), tmp.size());

    RpcCallback callback = std::bind(&Transport::Receive, this, std::placeholders::_1);
    clients_[hip]->call(&req, callback);
    return 0;
}

int Transport::Receive(response_t *rsp){
    raft::RaftMessage msg;
    msg.ParseFromString(rsp->data());
    Raft *raf = raft_server_->GetRaft(msg.raftid());
    if(raf==nullptr){
        fprintf(stderr, "RaftServer not found raftid:%d\n", msg.raftid());
    }
    switch(msg.msg_case()){
        case raft::RaftMessage::kAeRsp:
            raf->recvAppendEntriesResponse(msg.mutable_ae_rsp());
            break;
        case raft::RaftMessage::kVtRsp:
            raf->recvVoteResponse(msg.mutable_vt_rsp());
            break;
        case raft::RaftMessage::kMcRsp:
            raf->recvConfChangeResponse(msg.mutable_mc_rsp());
            break;
        default:
            fprintf(stderr, "unknown msg type:%d\n", msg.msg_case());
    }
    return 0;
}

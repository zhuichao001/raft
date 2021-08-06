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

void Transport::Send(const address_t *addr, const std::shared_ptr<raft::RaftMessage> msg){
    int64_t hip = addr->to_long();
    if(clients_.find(hip)==clients_.end()){
        dialer_t *cli = eng_->open(addr);
        clients_[hip] = cli;
    }

    string tmp;
    msg->SerializeToString(&tmp);

    raft::RaftMessage msg2;
    msg2.ParseFromString(tmp);
    auto req2 = msg2.mutable_ae_req();
    fprintf(stderr, "|||CHECK nodeid:%d, term:%d, commit_idx:%d\n", req2->nodeid(), req2->term(), req2->commit());

    request_t req;
    req.setbody(tmp.c_str(), tmp.size());

    RpcCallback callback = std::bind(&Transport::Receive, this, std::placeholders::_1);
    clients_[hip]->call(&req, callback);
}

int Transport::Receive(response_t *rsp){
    raft::RaftMessage msg;
    msg.ParseFromString(rsp->data());
    Raft *raf = raft_server_->GetRaft(msg.raftid());
    if(raf==nullptr){
        fprintf(stderr, "RaftServer not found raftid:%d\n", msg.raftid());
    }
    switch(msg.type()){
        case raft::RaftMessage::MSGTYPE_APPENDLOG_RESPONSE:
            raf->recvAppendEntriesResponse(msg.mutable_ae_rsp());
            break;
        case raft::RaftMessage::MSGTYPE_VOTE_RESPONSE:
            raf->recvVoteResponse(msg.mutable_vt_rsp());
            break;
        case raft::RaftMessage::MSGTYPE_CONFCHANGE_RESPONSE:
            raf->recvConfChangeResponse(msg.mutable_mc_rsp());
            break;
        default:
            fprintf(stderr, "unknown msg type:%d\n", msg.type());
    }
    return 0;
}

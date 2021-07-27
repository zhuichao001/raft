#include <stdio.h>
#include "transport.h"
#include "raft_node.h"
#include "raft_server.h"
#include "raft.h"

Transport::Transport(RaftServer *rs):
    raft_server_(rs){
}

int Transport::Start(address_t *addr, server_t *svr){
    if (addr == nullptr){
        return -1;
    }
    servers[addr] = svr;
    eng.start(addr, svr);
    return 0;
}

void Transport::Stop() {
    eng.stop();
}

void Transport::Run(){
    eng.run();
}

void Transport::Send(const RaftNode *to, const raft::RaftMessage *msg){
    const address_t *addr = to->GetAddress();
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

int Transport::dispatch(request_t *req, response_t *rsp){
    raft::RaftMessage msg;
    msg.ParseFromString(rsp->data());
    Raft *raf = raft_server_->GetRaft(msg.raftid()); //TODO
    if(raf==nullptr){
        fprintf(stderr, "RaftServer not found raftid:%d\n", msg.raftid());
    }
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

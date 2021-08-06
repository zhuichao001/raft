#ifndef _RAFT_SERVER_H_
#define _RAFT_SERVER_H_

#include <string>
#include <functional>
#include <stdio.h>
#include "lotus/server.h"
#include "lotus/engine.h"
#include "lotus/protocol.h"
#include "proto/raftmsg.pb.h"
#include "transport.h"
#include "raft.h"
#include "raft_sm.h"
#include "options.h"


class RaftServer : public server_t {
public:
    RaftServer(engine_t *eng):
        eng_(eng){
        trans_ = new Transport(eng_, this);
    }

    int Create(RaftOptions &opt, Raft **raft) {
        if(raft==nullptr){
            return -1;
        }

        trans_->Start(&opt.addr, dynamic_cast<server_t*>(this));

        //why the order affects timer's regular work
        opt.clocker = eng_;
        opt.tran = trans_;
        rafts_[opt.raftid] = new Raft(opt);

        *raft = rafts_[opt.raftid];
        return 0;
    }

    int Remove(int64_t raftid) {
        rafts_.erase(raftid); //TODO
        return 0; 
    }

    int ChangeMember(int raftid, raft::RaftLogType type, address_t *leader_addr, address_t *local_addr, int nodeid){

        auto msg = std::make_shared<raft::RaftMessage>();
        msg->set_raftid(raftid);
        msg->set_type(raft::RaftMessage::MSGTYPE_CONFCHANGE_REQUEST);

        auto mc_req = new raft::MemberChangeRequest;
        mc_req->set_type(type);

        raft::Peer *peer = mc_req->mutable_peer();
        peer->set_raftid(raftid);
        peer->set_nodeid(nodeid);
        peer->set_ip(local_addr->ip);
        peer->set_port(local_addr->port);

        msg->set_allocated_mc_req(mc_req);

        trans_->Send(leader_addr, msg);
    }

    Raft *GetRaft(int64_t raftid) {
        auto it = rafts_.find(raftid);
        if (it!=rafts_.end()) {
            return it->second;
        }
        return nullptr;
    }

    void Start() {
        eng_->run();
    }

    void Stop() {
        eng_->stop();
    }

public:
    int process(request_t *req, response_t *rsp) override {
        fprintf(stderr, "rpc server process.\n");
        fprintf(stderr, "rpc req=%s.\n", req->data());

        raft::RaftMessage in, out;

        in.ParseFromString(req->data());
        out.set_raftid(in.raftid());

        Raft *raft = GetRaft(in.raftid());
        if(raft==nullptr){
            rsp->seterrcode(-200);
            return -1;
        }

        switch(in.type()){
            case raft::RaftMessage::MSGTYPE_APPENDLOG_REQUEST:
                raft->recvAppendEntries(in.mutable_ae_req(), out.mutable_ae_rsp());
                out.set_type(raft::RaftMessage::MSGTYPE_APPENDLOG_RESPONSE);
                break;
            case raft::RaftMessage::MSGTYPE_VOTE_REQUEST:
                raft->recvVoteRequest(&in.vt_req(), out.mutable_vt_rsp());
                out.set_type(raft::RaftMessage::MSGTYPE_VOTE_RESPONSE);
                break;
            case raft::RaftMessage::MSGTYPE_CONFCHANGE_REQUEST:
                raft->recvConfChangeRequest(in.mutable_mc_req(), out.mutable_mc_rsp());
                out.set_type(raft::RaftMessage::MSGTYPE_CONFCHANGE_RESPONSE);
            default:
                fprintf(stderr, "unknown msg type:%d\n", in.type());
        }

        std::string tmp;
        out.SerializeToString(&tmp);
        rsp->setbody(tmp.c_str(), tmp.size());
        return 0;
    }

private:
    engine_t *eng_;
    std::map<uint64_t, Raft*> rafts_;
    Transport *trans_;

    friend Transport;
};

#endif

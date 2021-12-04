#ifndef _RAFT_SERVER_H_
#define _RAFT_SERVER_H_

#include <string>
#include <functional>
#include <stdio.h>
#include "lotus/service.h"
#include "lotus/engine.h"
#include "lotus/protocol.h"
#include "proto/raftmsg.pb.h"
#include "transport.h"
#include "raft.h"
#include "raft_sm.h"
#include "options.h"


class RaftServer : public service_t {
public:
    RaftServer(engine_t *eng):
        eng_(eng){
        trans_ = new Transport(eng_, this);
    }

    int Create(RaftOptions &opt, Raft **raft) {
        if(raft==nullptr){
            return -1;
        }

        trans_->Start(&opt.addr, dynamic_cast<service_t*>(this));

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

        auto mc_req = new raft::MemberChangeRequest;
        mc_req->set_type(type);

        raft::Peer *p = mc_req->mutable_peer();
        p->set_raftid(raftid);
        p->set_nodeid(nodeid);
        p->set_ip(local_addr->ip);
        p->set_port(local_addr->port);

        fprintf(stderr, "|||#### raftid:%d, nodeid:%d, ip:%s, port:%d\n", p->raftid(), p->nodeid(), p->ip().c_str(), p->port());

        msg->set_allocated_mc_req(mc_req);

        if(trans_->Send(leader_addr, msg)==0){
            fprintf(stderr, "SUCCESS JOIN CLUSTER raftid=%d, nodeid=%d leader=%s:%d\n", 
                    p->raftid(), p->nodeid(), p->ip().c_str(), p->port());
            return 0;
        } else {
            fprintf(stderr, "Error!FAILED TO JOIN CLUSTER raftid=%d, nodeid=%d leader=%s:%d\n", 
                    p->raftid(), p->nodeid(), p->ip().c_str(), p->port());
            return -1;
        }
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
        fprintf(stderr, "RECEIVE:%s len=%d\n", req->data(), req->len());

        auto in = std::make_shared<raft::RaftMessage>();
        auto out = std::make_shared<raft::RaftMessage>();

        in->ParseFromString(req->data());

        Raft *raft = GetRaft(in->raftid());
        if(raft==nullptr){
            rsp->seterrcode(-200);
            return -1;
        }
        out->set_raftid(in->raftid());

        switch(in->msg_case()){
            case raft::RaftMessage::kAeReq:
                raft->recvAppendEntries(in->mutable_ae_req(), out->mutable_ae_rsp());
                break;
            case raft::RaftMessage::kVtReq:
                raft->recvVoteRequest(&in->vt_req(), out->mutable_vt_rsp());
                break;
            case raft::RaftMessage::kMcReq:
                printf("MSGTYPE_CONFCHANGE_REQUEST deal\n");
                raft->recvConfChangeRequest(&in->mc_req(), out->mutable_mc_rsp());
            default:
                fprintf(stderr, "unknown msg type:%d\n", in->msg_case());
        }

        std::string tmp;
        out->SerializeToString(&tmp);
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
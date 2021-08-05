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
        rafts_[opt.id] = new Raft(opt);

        *raft = rafts_[opt.id];
        return 0;
    }

    int Remove(int64_t raftid) {
        rafts_.erase(raftid); //TODO
        return 0; 
    }

    int AddMember(int64_t raftid, raft::Peer *peer){
        Raft *raft = GetRaft(raftid);
        if(raft==nullptr){
            return -1;
        }

        raft->ChangeMember(raft::LOGTYPE_ADD_NODE, peer);
        //raft->addRaftNode(nodeid, addr, false);
        return 0;
    }

    int RemoveMember(int64_t raftid, const raft::Peer *peer){
        Raft *raft = GetRaft(raftid);
        if(raft==nullptr){
            return -1;
        }

        raft->ChangeMember(raft::LOGTYPE_REMOVE_NODE, peer);
        return 0;
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

        std::string tmp;
        switch(in.type()){
            case raft::RaftMessage::MSGTYPE_APPENDLOG_REQUEST:
                raft->recvAppendEntries(in.mutable_ae_req(), out.mutable_ae_rsp());
                out.set_type(raft::RaftMessage::MSGTYPE_APPENDLOG_RESPONSE);
                out.SerializeToString(&tmp);
                rsp->setbody(tmp.c_str(), tmp.size());
                break;
            case raft::RaftMessage::MSGTYPE_VOTE_REQUEST:
                raft->recvVoteRequest(&in.vt_req(), out.mutable_vt_rsp());
                out.set_type(raft::RaftMessage::MSGTYPE_VOTE_RESPONSE);
                out.SerializeToString(&tmp);
                rsp->setbody(tmp.c_str(), tmp.size());
                break;
            case raft::RaftMessage::MSGTYPE_HANDSHAKE_REQUEST:
                break;
            case raft::RaftMessage::MSGTYPE_CONFCHANGE_REQUEST:
                this->recvMemberChange(in.raftid(), in.mutable_mc_req());
            default:
                fprintf(stderr, "unknown msg type:%d\n", in.type());
        }
        return 0;
    }

    int recvMemberChange(uint64_t raftid, raft::MemberChangeRequest *req){
        if(req->type() == raft::LOGTYPE_ADD_NODE){
            AddMember(raftid, req->mutable_peer());
        } else if(req->type()==raft::LOGTYPE_REMOVE_NODE){
            RemoveMember(raftid, req->mutable_peer());
        }
    }

private:
    engine_t *eng_;
    std::map<uint64_t, Raft*> rafts_;
    Transport *trans_;

    friend Transport;
};

#endif

#include <iostream>
#include <string>
#include <thread>
#include <time.h>
#include <unistd.h>
#include "application.h"
#include "lotus/address.h"
#include "lotus/engine.h"
#include "raft/raft_server.h"

#include<stdio.h>
#include<getopt.h>

bool isleader = true;
int raftid = 117;
int nodeid;
int port;
int leader_port; //leader address

engine_t eng;

int parse_opt(int argc, char *argv[]) {
    int index = 0;
    int c = 0;

    struct  option long_options[] = {
        {"raftid",  required_argument,  NULL, 'r'},
        {"nodeid",  required_argument,  NULL, 'i'},
        {"join",    required_argument,  NULL, 'j'},
        {"help",    no_argument,        NULL, 'h'},
    };

    while(EOF != (c = getopt_long(argc,argv,"hri:j:", long_options, &index))) {
        switch(c) {
            case 'h':
                fprintf(stderr, "./server -i --join");
                break;
            case 'r':
                raftid = atoi(optarg);
                break;
            case 'i':
                fprintf(stderr, "-i:%s\n", optarg);
                nodeid = atoi(optarg);
                port = 9000+nodeid;
                break;
            case 'j':
                isleader = false;
                leader_port = atoi(optarg);
                break;
            case '?':
                printf("unknow option:%c\n",optopt);
                break;
            default:
                break;
        }
    }
    fprintf(stderr, "is_leader:%d, id:%d, port:%d, leader_port:%d\n", isleader, nodeid, port, leader_port);
    return 0;
}

    raft::RaftMessage msg;
    raft::MemberChangeRequest mc_req;
int add_raft_node(){
    address_t addr("127.0.0.1", leader_port);
    dialer_t *cli = eng.open(&addr);

    //raft::RaftMessage msg;
    msg.set_raftid(raftid);
    msg.set_type(raft::RaftMessage::MSGTYPE_CONFCHANGE_REQUEST);

    //raft::MemberChangeRequest mc_req;
    mc_req.set_type(raft::LOGTYPE_ADD_NODE);
    raft::Peer *peer = mc_req.mutable_peer();
    peer->set_raftid(raftid);
    peer->set_nodeid(nodeid);
    peer->set_ip("127.0.0.1");
    peer->set_port(port);

    msg.set_allocated_mc_req(&mc_req);

    string tmp;
    msg.SerializeToString(&tmp);

    request_t req;
    req.setbody(tmp.c_str(), tmp.size());

    cli->call(&req, nullptr);
}

int main(int argc, char *argv[]){
    parse_opt(argc, argv);

    RaftServer ras(&eng);
    Application app;
    RaftOptions opt;
    {
        opt.addr = address_t("0.0.0.0", port);
        opt.id = raftid;
        opt.nodeid = nodeid;
        opt.stm = &app;
    }

    ras.Create(opt, &app.raft_);
    std::thread th([=,&ras]{
        ras.Start();
    });

    if(!isleader){
        add_raft_node();
    }else{
        sleep(1);
        app.Set(std::string("abc"));
    }

    sleep(1);
    printf("get:%s\n", app.Get().c_str());

    while(true){
        char buf[128];
        printf("please input:");
        scanf("%s", buf);
        if(strcmp(buf, "exit")==0){
            break;
        }
        app.Set(std::string(buf));
        sleep(1);
        printf("get:%s\n", app.Get().c_str());
    }

    return 0;
}

#include <iostream>
#include <string>
#include <thread>
#include <time.h>
#include <unistd.h>
#include "application.h"
#include "lotus/address.h"
#include "lotus/engine.h"
#include "raft_server.h"

#include <stdio.h>
#include <getopt.h>

int raftid = 117;
int nodeid;
std::string local_ip = "127.0.0.1";
int local_port = 0;
int leader_port = 0; //leader address

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
                local_port = 9000+nodeid;
                break;
            case 'j':
                leader_port = atoi(optarg);
                break;
            case '?':
                printf("unknow option:%c\n",optopt);
                break;
            default:
                break;
        }
    }
    fprintf(stderr, "id:%d, port:%d, leader_port:%d\n", nodeid, local_port, leader_port);
    return 0;
}

std::string input(){
    char buf[128];
    printf("please input:");
    scanf("%s", buf);
    return buf;
}

int main(int argc, char *argv[]){
    parse_opt(argc, argv);

    RaftServer ras(&eng);
    Application app;
    RaftOptions opt;
    {
        opt.addr = address_t("0.0.0.0", local_port);
        opt.raftid = raftid;
        opt.nodeid = nodeid;
        opt.stm = &app;
    }

    ras.Create(opt, &app.raft_);
    std::thread th([=,&ras]{
        ras.Start();
    });

    if(leader_port!=0){ //join a leader
        address_t leader_addr(local_ip.c_str(), leader_port);
        ras.ChangeMember(raftid, raft::LOGTYPE_ADD_NODE, &leader_addr, &opt.addr, nodeid);
    }

    while(true){
        if(app.IsLeader()){
            string buf = input();
            app.Set(std::string(buf));
            sleep(1);
            app.Get();
        }else{
            sleep(2);
            app.Get();
        }
    }

    return 0;
}

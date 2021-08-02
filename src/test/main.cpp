#include <iostream>
#include <string>
#include <thread>
#include <time.h>
#include <unistd.h>
#include "application.h"
#include "lotus/address.h"
#include "raft/raft_server.h"

int main(){
    RaftServer ras;
    Application app;

    RaftOptions opt ;
    {
        opt.addr = new address_t("0.0.0.0", 5678);
        opt.id = 171;
        opt.stm = &app;
        opt.watcher = ras.GetTimeDriver();
    }

    ras.Create(opt, &app.raft_);
    sleep(2);

    std::thread th([=,&ras]{
        ras.Start();
    });

    app.Set(std::string("abc"));
    sleep(2);

    std::cout<<"get:"<<app.Get()<<std::endl;

    return 0;
}

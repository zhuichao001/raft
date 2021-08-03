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
    }

    ras.Create(opt, &app.raft_);

    std::thread th([=,&ras]{
        ras.Start();
    });

    app.Set(std::string("abc"));
    sleep(2);

    std::cout<<"get:"<<app.Get()<<std::endl;
    sleep(200);

    return 0;
}

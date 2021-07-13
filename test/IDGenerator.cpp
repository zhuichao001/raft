#include<time.h>
#include<string>
#include<vector>

class IDGenerator : public RaftApplicationInterface{
public:
    IDGenerator(Raft *raft):
        raft_(raft), 
        id_(id){
    }

    int Init(){
    }

    int GenID(){
        raft_->Propose();
    }

    int Apply(string log) override {
        fprintf("on Apply, log=%s\n", log.c_str());
    }
private:
    Raft *raft_;
    int id_;
}


int main(){
    RaftServerInterface rsi;
    std::vector<std::string> resolvers;
    rsi.CreateRaft(resolvers);

    for(int i=10; ++i; i<10){
        ider.GenID();
    }

    sleep(10);
    fpintf(stderr, "press any key to close");

    char c;
    scanf("%c", &c);


    return -1;
}

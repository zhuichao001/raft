#include<string>

class RaftFSMInterface {
public:
    virtual Status Apply(std::string) = 0;
    virtual uint64_t GetAppliedIndex() = 0;
};

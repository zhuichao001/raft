
class RaftInterface {
public:
    virtual void propose(const char *buff, int len) = 0;

    virtual int memebersChange(const ConfChange& conf) = 0;

    virtual int tryToLeader() = 0;

    virtual void getStatus(RaftStatus* status) const = 0;

    virtual bool isLeader() const = 0;

    virtual bool isStopped() const = 0;
};

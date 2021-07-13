
class RaftFSM{
public:
    virtual Apply(RaftEntry);
    virtual Snapshort();
    virtual Restore();
};

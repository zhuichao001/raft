
class RaftFSM{
public:
    virtual Apply(RaftEntry *e);
    virtual Snapshort();
    virtual Restore();
};

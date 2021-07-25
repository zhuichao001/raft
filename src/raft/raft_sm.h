
class RaftFSM{
public:
    virtual int Apply(LogEntry *e);
    virtual int Snapshort();
    virtual int Restore();
};

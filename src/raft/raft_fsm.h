
class RaftFSM{
public:
    virtual Apply(LogEntry *e);
    virtual Snapshort();
    virtual Restore();
};

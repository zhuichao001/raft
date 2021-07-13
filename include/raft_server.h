

class RaftServerInterface{
public:
        RaftServerInterface() = default;

        virtual ~RaftServerInterface() = default;

        virtual Status start() = 0;

        virtual Status stop() = 0;

        virtual Status createRaft(std::shared_ptr<Raft>* raft) = 0;

        virtual Status removeRaft(uint64_t id) = 0;

        virtual std::shared_ptr<Raft> findRaft(uint64_t id) const = 0;

        virtual void getStatus(ServerStatus* status) const = 0;
};


struct ReplicaStatus {
    uint64_t node_id = 0;
    uint64_t peer_id = 0;
    uint64_t peer_type = 0; //0 normal, 1 leader

    uint64_t match = 0;
    uint64_t commit = 0;
    uint64_t next = 0;
};

struct RaftStatus {
    uint64_t node_id = 0;
    uint64_t peer_id = 0;

    uint64_t leader = 0;
    uint64_t term = 0;

    uint64_t log_index = 0;  // log index
    uint64_t commit_index = 0;
    uint64_t applied_index = 0;

    std::map<uint64_t, ReplicaStatus> replicas; // key: peer_id
};

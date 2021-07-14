#include <string>



enum RaftLogType {
    RAFT_LOGTYPE_NORMAL,
    RAFT_LOGTYPE_ADD_NONVOTING_NODE,
    RAFT_LOGTYPE_ADD_NODE,
    RAFT_LOGTYPE_REMOVE_NODE,
    RAFT_LOGTYPE_NUM,
};

typedef struct {
    RaftLogType logtype;
    unsigned int term;
    unsigned int id;
    std::string data;
    bool isConfigChange(){
        return (logtype & (RAFT_LOGTYPE_ADD_NODE | RAFT_LOGTYPE_REMOVE_NODE)) !=0;
    }
} LogEntry;

typedef LogEntry LogEntryRequest;

typedef struct {
    unsigned int id;
    int term;
    int idx;
} LogEntryResponse;

typedef struct {
    int raft_port;
    int http_port;
    int node_id;
} HandshakeRequest;

typedef struct {
    int success;

    int leader_port;
    std::string leader_host;

    int node_id;
    int http_port;
} HandshakeResponse;

typedef struct {
    int term;
    int candidate_id;
    int last_log_idx;
    int last_log_term;
} VoteRequest;

typedef struct {
    int term;
    int granting; // true or false
} VoteResponse;

typedef struct {
    int term;
    int prev_log_idx;
    int prev_log_term;
    int leader_commit;

    LogEntry* entries;
    int n_entries;
} AppendEntriesRequest;

typedef struct {
    int term;
    int success; //true if match prev_log
    int current_idx;
    int first_idx;
} AppendEntriesResponse;


typedef struct {
    int type;
    union {
        HandshakeRequest hs_req;
        HandshakeResponse hs_rsp;

        VoteRequest vt_req;
        VoteResponse vt_rsp;

        AppendEntriesRequest ae_req;
        AppendEntriesResponse ae_rsp;
    };
} RaftMsg;

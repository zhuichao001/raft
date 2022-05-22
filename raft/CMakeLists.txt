cmake_minimum_required(VERSION 3.10)

project(raft C CXX)

set(CXX_FLAGS
    -g
    -DCHECK_PTHREAD_RETURN_VALUE
    -D_FILE_OFFSET_BITS=64
    -Wall
    -Wextra
    -Werror
    -Wconversion
    -Wno-unused-parameter
    -Wold-style-cast
    -Woverloaded-virtual
    -Wpointer-arith
    -Wshadow
    -Wwrite-strings
    -march=native
    -std=c++14
    -rdynamic
)

include_directories(../../../zhuichao001/naughty/fio)
#add_subdirectory(../../../zhuichao001/naughty/fio fio_lib_dir)

include_directories(../../../zhuichao001/wal)

include_directories(../../../zhuichao001/lotus/)
#add_subdirectory(../../../zhuichao001/lotus/lotus lotus_lib_dir)

include_directories(./proto)
add_subdirectory(./proto raftproto_lib_dir)

aux_source_directory(./ DIR_LIB_RAFT_SRCS)
add_library(raft STATIC ${DIR_LIB_RAFT_SRCS})

target_link_libraries(raft PUBLIC raftproto)

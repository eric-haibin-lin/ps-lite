function cleanup() {
    echo "kill all testing process of ps lite for user $USER"
    if [[ $EUID -ne 0 ]]; then
        pkill -9 -u $USER -f stress_test_benchmark
        pkill -9 -u $USER -f test_benchmark
    else
        pkill -9 -f stress_test_benchmark
        pkill -9 -f test_benchmark
    fi
    sleep 1
}
trap cleanup EXIT
# cleanup # cleanup on startup

export ARGS=${ARGS:-"4096000 102400 2"}
export DMLC_NUM_WORKER=1
export DMLC_NUM_SERVER=1
# export DMLC_NUM_WORKER=2
# export DMLC_NUM_SERVER=2
# export BYTEPS_ENABLE_IPC=1

export UCX_HOME=/home/tiger/haibin.lin/ps-lite-test-benchmark/ucx_build_master_cuda
export CUDA_HOME=/usr/local/cuda
export LD_LIBRARY_PATH=$UCX_HOME/lib:$CUDA_HOME/lib64

# export NODE_ONE_IP=10.0.0.1 # sched and server
# export NODE_TWO_IP=10.0.0.2 # worker

export DMLC_PS_ROOT_URI=${NODE_ONE_IP}  # try eth2
export BYTEPS_ORDERED_HOSTS=${NODE_ONE_IP},${NODE_TWO_IP}
export DMLC_NODE_HOST=${NODE_TWO_IP}  # by default it's remote
export UCX_RDMA_CM_SOURCE_ADDRESS=${NODE_TWO_IP}
export UCX_IB_TRAFFIC_CLASS=236

export DMLC_PS_ROOT_PORT=${DMLC_PS_ROOT_PORT:-19988}     # scheduler's port (can random choose)
export DMLC_INTERFACE=${DMLC_INTERFACE:-eth0}        # my RDMA interface
export DMLC_ENABLE_RDMA=1
export DMLC_ENABLE_UCX=${DMLC_ENABLE_UCX:-1}          # test ucx
# export PS_VERBOSE=2
# export UCX_TLS=all                # not working
# export UCX_TLS=ib,tcp           # working
#export UCX_TLS=ib,tcp,cuda_ipc,cuda_copy
export UCX_TLS=ib,cuda_ipc,cuda_copy
export UCX_TLS=ib,cuda
export UCX_TLS=ib,rc_x,cuda
# export UCX_MEMTYPE_CACHE=n
export UCX_RNDV_SCHEME=put_zcopy
#export BYTEPS_UCX_SHORT_THRESH=0
export UCX_NET_DEVICES=mlx5_1:1
export UCX_MAX_RNDV_RAILS=4

export LOG_DURATION=20
export LOCAL_SIZE=${LOCAL_SIZE:-2}               # test ucx gdr
#export CUDA_VISIBLE_DEVICES=0,1,2,3
export CUDA_VISIBLE_DEVICES=0,1
#export UCX_IB_GPU_DIRECT_RDMA=no
#export UCX_IB_GPU_DIRECT_RDMA=yes

export BYTEPS_NODE_ID=1
export TOTAL_DURATION=2000

if [ $# -eq 0 ] # no other args
then
    # launch scheduler
    echo "This is scheduler node."
    export BYTEPS_NODE_ID=0
    export DMLC_NODE_HOST=${NODE_ONE_IP}
    export UCX_RDMA_CM_SOURCE_ADDRESS=${NODE_ONE_IP}
    DMLC_ROLE=scheduler ./test_benchmark &

    if [ $DMLC_NUM_WORKER == "2" ]; then
        DMLC_ROLE=worker BENCHMARK_NTHREAD=1 ./test_benchmark $ARGS &
    fi
    # launch server
    DMLC_ROLE=server ./test_benchmark 
fi


if [ $DMLC_NUM_WORKER == "2" ]; then
    DMLC_ROLE=server BENCHMARK_NTHREAD=1 ./test_benchmark &
fi

# launch worker, with 30MB data per push pull, 10000 rounds, push_pull mode
DMLC_ROLE=worker BENCHMARK_NTHREAD=1 ./test_benchmark $ARGS

# for correctness test, use this following line and replace previous
# scheduler / server binary with ./test_correctness

# DMLC_ROLE=worker BENCHMARK_NTHREAD=1 ./test_correctness 30000000

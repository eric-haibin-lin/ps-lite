# scheduler's port
set -x
export DMLC_PS_ROOT_PORT=$ARNOLD_WORKER_0_PORT
export DMLC_ENABLE_RDMA=zmq
export BYTEPS_ENABLE_IPC=0

# scheduler's port
set -x
# export DMLC_NODE_HOST=10.188.136.14
# set DMLC_PS_ROOT_URI to the `DMLC_NODE_HOST` value of one of the hosts
export DMLC_PS_ROOT_URI=10.188.136.14
export DMLC_PS_ROOT_PORT=9999
export DMLC_ENABLE_RDMA=1
export BYTEPS_ENABLE_IPC=0
export DMLC_NUM_WORKER=1
export DMLC_NUM_SERVER=1
export PS_VERBOSE=1
export DMLC_INTERFACE=eth2
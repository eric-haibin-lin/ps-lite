source port_util.sh
export DMLC_NUM_WORKER=$1
export DMLC_NUM_SERVER=$2
export DMLC_PS_ROOT_URI=10.188.181.38
export DMLC_INTERFACE=eth0
DMLC_ROLE=server DMLC_ENABLE_UCX=0 GLOBAL_RANK=$3 ENABLE_GLOBAL_RANK=1 PS_VERBOSE=1 ./test_benchmark_provide_rank

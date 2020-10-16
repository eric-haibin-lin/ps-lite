source port_util.sh
export DMLC_NUM_WORKER=1
export DMLC_NUM_SERVER=1 
export DMLC_PS_ROOT_URI=10.188.136.21
export DMLC_INTERFACE=eth0
DMLC_ROLE=server DMLC_ENABLE_UCX=0 RANK=0 PS_VERBOSE=2 ./test_benchmark

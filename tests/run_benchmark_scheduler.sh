source port_util.sh
export DMLC_NUM_WORKER=1
export DMLC_NUM_SERVER=1 
export DMLC_PS_ROOT_URI=$ARNOLD_WORKER_0_HOST
export DMLC_INTERFACE=eth0
DMLC_ROLE=scheduler DMLC_ENABLE_UCX=0 ./test_benchmark

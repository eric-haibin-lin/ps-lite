DMLC_NUM_WORKER=1 DMLC_NUM_SERVER=3 BENCHMARK_NTHREAD=1 NUM_KEY_PER_SERVER=400 DMLC_ENABLE_UCX=1 UCX_NET_DEVICES=mlx5_1:1,mlx5_0:1 DMLC_PS_ROOT_PORT=12350 ARGS="25600 1000000 2 " BYTEPS_UCX_NUMA_AWARE=-1 NODE_TWO_IP=10.212.255.188 NODE_ONE_IP=10.212.255.190 UCX_HOME=/root/hpcx-v2.8.0-gcc-MLNX_OFED_LINUX-5.2-1.0.4.0-ubuntu16.04-x86_64/ucx/mt numactl -N 1 -m 1 bash test.sh local &

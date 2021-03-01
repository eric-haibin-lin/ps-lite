if [ $1 == "local8" ]; then
    NUM_KEY_PER_SERVER=1 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 0-31   bash test_np4.sh scheduler &
    NUM_KEY_PER_SERVER=1 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 32-63  bash test_np4.sh local &
    NUM_KEY_PER_SERVER=1 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 64-95  bash test_np4.sh local &
    NUM_KEY_PER_SERVER=1 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 96-127 bash test_np4.sh local &
elif [ $1 == "remote8" ]; then
    NUM_KEY_PER_SERVER=1 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 0-31   bash test_np4.sh remote &
    NUM_KEY_PER_SERVER=1 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 32-63  bash test_np4.sh remote &
    NUM_KEY_PER_SERVER=1 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 64-95  bash test_np4.sh remote &
    NUM_KEY_PER_SERVER=1 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 96-127 bash test_np4.sh remote &
elif [ $1 == "local4" ]; then
    DMLC_NUM_WORKER=4 NUM_KEY_PER_SERVER=1 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 32-47 bash test_np4.sh scheduler &
    DMLC_NUM_WORKER=4 NUM_KEY_PER_SERVER=1 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 48-63 bash test_np4.sh local &
elif [ $1 == "remote4" ]; then
    DMLC_NUM_WORKER=4 NUM_KEY_PER_SERVER=1 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 32-47 bash test_np4.sh remote &
    DMLC_NUM_WORKER=4 NUM_KEY_PER_SERVER=1 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 48-63 bash test_np4.sh remote &
elif [ $1 == "local2" ]; then
    DMLC_NUM_WORKER=2 NUM_KEY_PER_SERVER=100 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 32-63 bash test_np4.sh scheduler &
elif [ $1 == "remote2" ]; then
    DMLC_NUM_WORKER=2 NUM_KEY_PER_SERVER=100 NODE_ONE_IP=10.212.179.138 NODE_TWO_IP=10.212.179.130 taskset -c 96-127 bash test_np4.sh remote &
fi

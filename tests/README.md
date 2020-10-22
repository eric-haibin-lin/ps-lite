How to use

build by `make test` in the root directory, then run

```bash
find test_* -type f -executable -exec ./repeat.sh 4 ./local.sh 2 2 ./{} \;
```




## passing global rank test
1. working or not
    by running multi_scheduler.sh and multi_server_worker_test.sh


    ```
    bash multi_scheduler.sh &
    multi_server_worker_test.sh &
    ```

    you can get the feedback of the test working well or not.

2. rank assign logic works well

    To see the global rank you assigned is passing right to place where it belongs and was sent to the right place. You should run the test as below.

    first, start scheduler
    ```
    bash ./run_benchmark_scheduler_provide_rank.sh 3 3
    ```

    then, run 3 different woker and 3 different servers on different shell window to see the indepent result.

    shell 1
    ```
    bash ./run_benchmark_server_provide_rank.sh 3 3 0
    ```

    shell 2 
    ```
    bash ./run_benchmark_server_provide_rank.sh 3 3 1
    ```

    skipped

    shell 6

    ```
    bash ./run_benchmark_worker_provide_rank.sh 3 3 2
    ```


    you can get the following log from each shell window to see which rank is assign to which and how scheduler gather and scatter the global rank.

    ```
    [20:33:00] src/./zmq_van.h:149: Zmq skipped connection to node [role=server, id=12, ip=10.188.137.204, port=65317, is_recovery=0, aux_id=2]. My node is [role=server, id=8, ip=10.188.137.204, port=11895, is_recovery=0, aux_id=0]

    [20:33:00] src/./zmq_van.h:149: Zmq skipped connection to node [role=server, id=12, ip=10.188.137.204, port=65317, is_recovery=0, aux_id=2]. My node is [role=server, id=10, ip=10.188.137.204, port=37689, is_recovery=0, aux_id=1]

    [20:33:00] src/./zmq_van.h:149: Zmq skipped connection to node [role=server, id=8, ip=10.188.137.204, port=11895, is_recovery=0, aux_id=0]. My node is [role=server, id=12, ip=10.188.137.204, port=65317, is_recovery=0, aux_id=2]

    [20:33:00] src/./zmq_van.h:149: Zmq skipped connection to node [role=worker, id=11, ip=10.188.137.204, port=39587, is_recovery=0, aux_id=1]. My node is [role=worker, id=9, ip=10.188.137.204, port=23443, is_recovery=0, aux_id=0]

    [20:33:00] src/./zmq_van.h:149: Zmq skipped connection to node [role=worker, id=13, ip=10.188.137.204, port=52189, is_recovery=0, aux_id=2]. My node is [role=worker, id=11, ip=10.188.137.204, port=39587, is_recovery=0, aux_id=1]


    [20:33:00] src/./zmq_van.h:149: Zmq skipped connection to node [role=worker, id=9, ip=10.188.137.204, port=23443, is_recovery=0, aux_id=0]. My node is [role=worker, id=13, ip=10.188.137.204, port=52189, is_recovery=0, aux_id=2]
    ```

    which indicates how the global rank was using and gather and scatter from scheduler.
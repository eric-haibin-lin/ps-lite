bash ./run_benchmark_server_provide_rank.sh 3 3 0 &
bash ./run_benchmark_server_provide_rank.sh 3 3 1 &
bash ./run_benchmark_server_provide_rank.sh 3 3 2 &

bash ./run_benchmark_worker_provide_rank.sh 3 3 0 &
bash ./run_benchmark_worker_provide_rank.sh 3 3 1 &
bash ./run_benchmark_worker_provide_rank.sh 3 3 2 &

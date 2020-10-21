bash ./run_benchmark_server.sh 3 3 0 &
bash ./run_benchmark_server.sh 3 3 1 &
bash ./run_benchmark_server.sh 3 3 2 &

bash ./run_benchmark_worker.sh 3 3 0 &
bash ./run_benchmark_worker.sh 3 3 1 &
bash ./run_benchmark_worker.sh 3 3 2 &

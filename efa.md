

## Build with EFA

AMI: bert-hvd-efa-al2 in us-west-2

```
make clean; USE_FABRIC=1 make -j;
```

## Run Test
```
killall test_kv_app_benchmark; PS_VERBOSE=1 ENABLE_RDMA_LOG=1 DMLC_ENABLE_FABRIC=1 bash tests/local_multi_workers.sh 1 1 tests/test_kv_app_benchmark 1024000 10
```

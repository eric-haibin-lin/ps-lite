cc_library(
    name="ps",
    srcs=["src/*.cc"],
    incs=["src",
          "include"],
    export_incs=["include"],
    deps=[
          "cpp3rdlib/ucx:ucx-7125ef1-rdma-core-5.2-cuda10@//cpp3rdlib/ucx:ucp,uct,ucs,ucm",
          "cpp3rdlib/zmq:4.1.4@//cpp3rdlib/zmq:zmq",
          "cpp3rdlib/glog:0.3.3@//cpp3rdlib/glog:glog",
          "cpp3rdlib/rdma-core:ofed-5.2-gcc4@//cpp3rdlib/rdma-core:rdmacm,ibverbs",
          ],
    custom_deps= {
        "x86_64-gcc830": [
            "cpp3rdlib/zmq:4.2.2-gcc8@//cpp3rdlib/zmq:zmq",
            "cpp3rdlib/rdma-core:22.1-gcc8@//cpp3rdlib/rdma-core:rdmacm,ibverbs",
        ],
    },
    defs=["DMLC_USE_RDMA", "DMLC_USE_UCX"],
    optimize=["-O3", "-g", "-fopenmp", "-Wall", "-Wextra", "-std=c++14"],
    extra_linkflags=["-ldl"],
)

cc_test(
    name="test_benchmark",
    srcs=["tests/test_benchmark.cc"],
    incs=["src",
          "include"],
    deps=[":ps",
          "#pthread",
          "cpp3rdlib/ucx:ucx-7125ef1-rdma-core-5.2-cuda10@//cpp3rdlib/ucx:ucp,uct,ucs,ucm",
          "cpp3rdlib/zmq:4.1.4@//cpp3rdlib/zmq:zmq",
          "cpp3rdlib/glog:0.3.3@//cpp3rdlib/glog:glog",
          "cpp3rdlib/rdma-core:ofed-5.2-gcc4@//cpp3rdlib/rdma-core:rdmacm,ibverbs",
          "cpp3rdlib/cuda:10.0.130@//cpp3rdlib/cuda:cublas,cufft,curand,cusolver,cudart,nvToolsExt",
          ],
    defs=["DMLC_USE_RDMA", "DMLC_USE_UCX"],
    optimize=["-O3", "-g", "-fopenmp", "-Wall", "-Wextra", "-std=c++14"],
    extra_linkflags=["-ldl", "-lrt"],
    bundle_path="bundle",
)

cc_binary(
    name="test_benchmark_stress",
    srcs=["tests/test_benchmark_stress.cc"],
    incs=["src",
          "include"],
    deps=[":ps",
          "#pthread",
          "cpp3rdlib/ucx:ucx-7125ef1-rdma-core-5.2-cuda10@//cpp3rdlib/ucx:ucp,uct,ucs,ucm",
          "cpp3rdlib/zmq:4.1.4@//cpp3rdlib/zmq:zmq",
          "cpp3rdlib/glog:0.3.3@//cpp3rdlib/glog:glog",
          "cpp3rdlib/rdma-core:ofed-5.2-gcc4@//cpp3rdlib/rdma-core:rdmacm,ibverbs",
          ],
    defs=["DMLC_USE_RDMA", "DMLC_USE_UCX"],
    optimize=["-O3", "-g", "-fopenmp", "-Wall", "-Wextra", "-std=c++14"],
    extra_linkflags=["-ldl", "-lrt"],
    bundle_path="bundle",
)

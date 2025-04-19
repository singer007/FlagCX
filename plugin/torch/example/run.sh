#!/bin/bash

# Check if the debug flag is provided as an argument
if [ "$1" == "debug" ]; then
    export NCCL_DEBUG=INFO
    export NCCL_DEBUG_SUBSYS=all
    echo "NCCL debug information enabled."
else
    unset NCCL_DEBUG
    unset NCCL_DEBUG_SUBSYS
    echo "NCCL debug information disabled."
fi

export FLAGCX_DEBUG=INFO
export FLAGCX_DEBUG_SUBSYS=ALL
export CUDA_VISIBLE_DEVICES=0,1,2,3,4,5,6,7
# Need to preload customized gloo library specified for FlagCX linkage
# export LD_PRELOAD=/usr/local/lib/libgloo.so
# export LD_PRELOAD=/usr/local/nccl/build/lib/libnccl.so
export TORCH_DISTRIBUTED_DETAIL=DEBUG
export GLOO_SOCKET_IFNAME=ens42f0
CMD='torchrun --nproc_per_node 8 --nnodes=1 --node_rank=0 --master_addr="10.9.113.196" --master_port=12355 example_gcu.py'

echo $CMD
eval $CMD

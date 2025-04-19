#include "mpi.h"
#include "flagcx.h"
#include "tools.h"
#include <iostream>
#include <cstring>

#define DATATYPE flagcxFloat


int main(int argc, char *argv[]){
    parser args(argc, argv);
    size_t min_bytes = args.getMinBytes();
    size_t max_bytes = args.getMaxBytes();
    int step_factor = args.getStepFactor();
    int num_warmup_iters = args.getWarmupIters();
    int num_iters = args.getTestIters();
    //int print_buffer = args.isPrintBuffer();
    int print_buffer = 1;

    int totalProcs, proc; 
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &totalProcs);
    MPI_Comm_rank(MPI_COMM_WORLD, &proc);
    printf("I am %d of %d\n", proc, totalProcs);

    flagcxResult_t status;

    flagcxHandlerGroup_t handler;
    status = flagcxHandleInit(&handler);
    CHECK_NZ_EXIT(status, "flagcxHandleInit() failed %d\n", status);

    flagcxUniqueId_t& uniqueId = handler->uniqueId;
    flagcxComm_t& comm = handler->comm;
    flagcxDeviceHandle_t& devHandle = handler->devHandle;

    int localRank = 0;
    char* env = std::getenv("OMPI_COMM_WORLD_LOCAL_RANK");
    if (env) {
      localRank = atoi(env);
    }
    printf("rank:%d, local_rank:%d\n", proc, localRank);

    // int nGpu;
    // status = devHandle->getDeviceCount(&nGpu);
    // CHECK_NZ_EXIT(status, "getDeviceCount() failed %d\n", status);
    status = devHandle->setDevice(localRank); // proc % nGpu);
    CHECK_NZ_EXIT(status, "setDevice() failed %d\n", status);

    if (proc == 0)
        flagcxGetUniqueId(&uniqueId);
    MPI_Bcast((void *)uniqueId, sizeof(flagcxUniqueId), MPI_BYTE, 0, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
    
    status = flagcxCommInitRank(&comm, totalProcs, uniqueId, proc);
    CHECK_NZ_EXIT(status, "flagcxCommInitRank() failed %d\n", status);

    flagcxStream_t stream;
    status = devHandle->streamCreate(&stream);
    CHECK_NZ_EXIT(status, "streamCreate() failed %d\n", status);

    void *sendbuff, *recvbuff, *hello;
    size_t count;
    timer tim;
    
    for (size_t size = min_bytes; size <= max_bytes; size *= step_factor) {
        count = size / sizeof(float);
        status = devHandle->deviceMalloc(&sendbuff, size / totalProcs, flagcxMemDevice, NULL);
        CHECK_NZ_EXIT(status, "deviceMalloc() Device failed %d\n", status);
        status = devHandle->deviceMalloc(&recvbuff, size, flagcxMemDevice, NULL);
        CHECK_NZ_EXIT(status, "deviceMalloc() Device failed %d\n", status);
        status = devHandle->deviceMalloc(&hello, size, flagcxMemHost, NULL);
        CHECK_NZ_EXIT(status, "deviceMalloc() Host failed %d\n", status);
        status = devHandle->deviceMemset(hello, 0, size, flagcxMemHost, NULL);
        CHECK_NZ_EXIT(status, "deviceMemset() failed %d\n", status);

	    ((float *)hello)[0] = proc;

        status = devHandle->deviceMemcpy(sendbuff, hello, size / totalProcs, flagcxMemcpyHostToDevice, NULL);
        CHECK_NZ_EXIT(status, "deviceMemcpy() failed %d\n", status);
    
        if (proc == 0 && print_buffer) {
            printf("sendbuff = ");
        	  for (int i = 0; i < totalProcs; i++) {
                printf("%f ", ((float *)hello)[i * (count / totalProcs)]);
            }
            printf("\n");
           // printf("%f\n", ((float *)hello)[0]);
        }

        for(int i=0;i<num_warmup_iters;i++){
            status = flagcxAllGather(sendbuff, recvbuff, count / totalProcs, DATATYPE, comm, stream);
            CHECK_NZ_EXIT(status, "flagcxAllGather() failed %d\n", status);
        }
        status = devHandle->streamSynchronize(stream);
        CHECK_NZ_EXIT(status, "streamSynchronize() failed %d\n", status);

        MPI_Barrier(MPI_COMM_WORLD);

        tim.reset();
        for(int i=0;i<num_iters;i++){
            status = flagcxAllGather(sendbuff, recvbuff, count / totalProcs, DATATYPE, comm, stream);
            CHECK_NZ_EXIT(status, "flagcxAllGather() failed %d\n", status);
        }
        status = devHandle->streamSynchronize(stream);
        CHECK_NZ_EXIT(status, "streamSynchronize() failed %d\n", status);

        double elapsed_time = tim.elapsed() / num_iters;
        double base_bw = (double)(size) / 1.0E9 / elapsed_time;
        double alg_bw = base_bw;
        double factor = ((double)(totalProcs - 1))/((double)totalProcs);
        double bus_bw = base_bw * factor;
        if (proc == 0) {
            printf("Comm size: %zu bytes; Elapsed time: %lf sec; Algo bandwidth: %lf GB/s; Bus bandwidth: %lf GB/s\n", size, elapsed_time, alg_bw, bus_bw);
        }

        MPI_Barrier(MPI_COMM_WORLD);

        status = devHandle->deviceMemset(hello, 0, size, flagcxMemHost, NULL);
        CHECK_NZ_EXIT(status, "deviceMemset() failed %d\n", status);
        status = devHandle->deviceMemcpy(hello, recvbuff, size, flagcxMemcpyDeviceToHost, NULL);
        CHECK_NZ_EXIT(status, "deviceMemcpy() failed %d\n", status);
        if (proc == 0 && print_buffer) {
            printf("recvbuff = ");
        	for (int i = 0; i < totalProcs; i++) {
                printf("%f ", ((float *)hello)[i * (count / totalProcs)]);
            }
            printf("\n");
        }

        devHandle->deviceFree(sendbuff, flagcxMemDevice, NULL);
        devHandle->deviceFree(recvbuff, flagcxMemDevice, NULL);
        devHandle->deviceFree(hello, flagcxMemHost, NULL);
    }

    devHandle->streamDestroy(stream);
    flagcxCommDestroy(comm);
    flagcxHandleFree(handler);

    MPI_Finalize();
    return 0;
}

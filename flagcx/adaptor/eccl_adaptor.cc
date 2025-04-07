#ifdef USE_ENFLAME_ADAPTOR

#include "enflame_adaptor.h"

std::map<flagcxDataType_t, ecclDataType_t> f2e_datatype_map = {
    {flagcxInt8, ecclInt8},
    {flagcxHalf, ecclHalf},
    {flagcxFloat16, ecclFloat16},
    {flagcxBfloat16, ecclBfloat16},
    {flagcxFloat32, ecclFloat32},
    {flagcxFloat, ecclFloat},
};

std::map<flagcxRedOp_t, ecclRedOp_t> f2e_reduceop_map = {
    {flagcxSum, ecclSum},
    {flagcxProd, ecclProd},
    {flagcxMax, ecclMax},
    {flagcxMin, ecclMin},
    {flagcxAvg, ecclAvg},
};

std::map<ecclResult_t, flagcxResult_t> e2f_ret_map = {
    {ecclSuccess, flagcxSuccess},
    {ecclUnhandledTopsError, flagcxUnhandledDeviceError},
    {ecclSystemError, flagcxSystemError},
    {ecclInternalError, flagcxInternalError},
    {ecclInvalidArgument, flagcxInvalidArgument},
    {ecclInvalidUsage, flagcxInvalidUsage},
    {ecclRemoteError , flagcxRemoteError},
    {ecclInProgress , flagcxInProgress},
    {ecclNumResults , flagcxNumResults},
};

std::map<flagcxResult_t, ecclResult_t> f2e_ret_map = {
    {flagcxSuccess, ecclSuccess},
    {flagcxUnhandledDeviceError, ecclUnhandledTopsError}
};

flagcxResult_t ecclAdaptorGetVersion(int *version) {
    return (flagcxResult_t)ecclGetVersion(version);
}

flagcxResult_t ecclAdaptorGetUniqueId(flagcxUniqueId_t *uniqueId) {
    if (*uniqueId == NULL) {
        flagcxCalloc(uniqueId, 1);
    }
    return (flagcxResult_t)ecclGetUniqueId((ecclUniqueId *)(*uniqueId));
}

const char* ecclAdaptorGetErrorString(flagcxResult_t result) {
    return ecclGetErrorString((ecclResult_t)f2e_ret_map[result]);
}

const char* ecclAdaptorGetLastError(flagcxInnerComm_t comm) {
    return ecclGetLastError(comm->base);
}

flagcxResult_t ecclAdaptorCommInitRank(flagcxInnerComm_t *comm, int nranks, flagcxUniqueId_t commId, int rank, bootstrapState */*bootstrap*/) {
    if (*comm == NULL) {
        flagcxCalloc(comm, 1);
    }
    return (flagcxResult_t)e2f_ret_map[ecclCommInitRank(&(*comm)->base, nranks, *(ecclUniqueId *)commId, rank)];
}

//TODO: unsupported
flagcxResult_t ecclAdaptorCommFinalize(flagcxInnerComm_t comm) {
    return flagcxUnhandledDeviceError;
}

flagcxResult_t ecclAdaptorCommDestroy(flagcxInnerComm_t comm) {
    return (flagcxResult_t)e2f_ret_map[ecclCommDestroy(comm->base)];
}

flagcxResult_t ecclAdaptorCommAbort(flagcxInnerComm_t comm) {
    return (flagcxResult_t)e2f_ret_map[ecclCommAbort(comm->base)];
}

//TODO: unsupported
flagcxResult_t ecclAdaptorCommResume(flagcxInnerComm_t comm) {
    return flagcxUnhandledDeviceError;
}

//TODO: unsupported
flagcxResult_t ecclAdaptorCommSuspend(flagcxInnerComm_t comm) {
    return flagcxUnhandledDeviceError;
}

flagcxResult_t ecclAdaptorCommCount(const flagcxInnerComm_t comm, int* count) {
    return (flagcxResult_t)e2f_ret_map[ecclCommCount(comm->base, count)];
}

flagcxResult_t ecclAdaptorCommCuDevice(const flagcxInnerComm_t comm, int* device) {
    return (flagcxResult_t)e2f_ret_map[ecclCommDevice(comm->base, device)];
}

flagcxResult_t ecclAdaptorCommUserRank(const flagcxInnerComm_t comm, int* rank) {
    return (flagcxResult_t)e2f_ret_map[ecclCommUserRank(comm->base, rank)];
}

flagcxResult_t ecclAdaptorCommGetAsyncError(flagcxInnerComm_t comm, flagcxResult_t asyncError) {
    return (flagcxResult_t)e2f_ret_map[ecclCommGetAsyncError(comm->base, (ecclResult_t *)&asyncError)];
}

flagcxResult_t ecclAdaptorReduce(const void* sendbuff, void* recvbuff, size_t count,
                                 flagcxDataType_t datatype, flagcxRedOp_t op, int root,
                                 flagcxInnerComm_t comm, flagcxStream_t stream) {
    return (flagcxResult_t)ecclReduce(sendbuff, recvbuff, count, (ecclDataType_t)datatype, (ecclRedOp_t)op, root, comm->base, stream->base);
}

flagcxResult_t ecclAdaptorGather(const void* sendbuff, void* recvbuff, size_t count,
                                 flagcxDataType_t datatype, int root, flagcxInnerComm_t comm,
                                 flagcxStream_t stream) {
    int rank, nranks;
    ecclResult_t res = ecclSuccess;
    res = ecclCommUserRank(comm->base, &rank);
    res = ecclCommCount(comm->base, &nranks);

    size_t size = count * getFlagcxDataTypeSize(datatype);
    char* buffer = static_cast<char*>(recvbuff);

    res = ecclGroupStart();
    if (rank == root) {
        for (int r = 0; r < nranks; r++) {
            res = ecclRecv(static_cast<void*>(buffer + r * size), size, ecclChar, r, comm->base, stream->base);
        }
    }
    res = ecclSend(sendbuff, size, ecclChar, root, comm->base, stream->base);
    res = ecclGroupEnd();

    return (flagcxResult_t)res;
}

flagcxResult_t ecclAdaptorScatter(const void* sendbuff, void* recvbuff, size_t count,
                                  flagcxDataType_t datatype, int root, flagcxInnerComm_t comm,
                                  flagcxStream_t stream) {
    int rank, nranks;
    ecclResult_t res = ecclSuccess;
    res = ecclCommUserRank(comm->base, &rank);
    res = ecclCommCount(comm->base, &nranks);

    size_t size = count * getFlagcxDataTypeSize(datatype);
    const char* buffer = static_cast<const char*>(sendbuff);

    res = ecclGroupStart();
    if (rank == root) {
        for (int r = 0; r < nranks; r++) {
            res = ecclSend(static_cast<const void*>(buffer + r * size), size, ecclChar, r, comm->base, stream->base);
        }
    }
    res = ecclRecv(recvbuff, size, ecclChar, root, comm->base, stream->base);
    res = ecclGroupEnd();

    return (flagcxResult_t)res;
}

flagcxResult_t ecclAdaptorBroadcast(const void* sendbuff, void* recvbuff, size_t count,
                                    flagcxDataType_t datatype, int root, flagcxInnerComm_t comm,
                                    flagcxStream_t stream) {
    return (flagcxResult_t)ecclBroadcast(sendbuff, recvbuff, count, (ecclDataType_t)datatype, root, comm->base, stream->base);
}

flagcxResult_t ecclAdaptorAllReduce(const void* sendbuff, void* recvbuff, size_t count,
                                    flagcxDataType_t datatype, flagcxRedOp_t op,
                                    flagcxInnerComm_t comm, flagcxStream_t stream) {
    return (flagcxResult_t)ecclAllReduce(sendbuff, recvbuff, count, (ecclDataType_t)datatype, (ecclRedOp_t)op, comm->base, stream->base);
}

flagcxResult_t ecclAdaptorReduceScatter(const void* sendbuff, void* recvbuff, size_t recvcount,
                                        flagcxDataType_t datatype, flagcxRedOp_t op,
                                        flagcxInnerComm_t comm, flagcxStream_t stream) {
    return (flagcxResult_t)ecclReduceScatter(sendbuff, recvbuff, recvcount, (ecclDataType_t)datatype, (ecclRedOp_t)op, comm->base, stream->base);
}

flagcxResult_t ecclAdaptorAllGather(const void* sendbuff, void* recvbuff, size_t sendcount,
                                    flagcxDataType_t datatype, flagcxInnerComm_t comm,
                                    flagcxStream_t stream) {
    return (flagcxResult_t)ecclAllGather(sendbuff, recvbuff, sendcount, (ecclDataType_t)datatype, comm->base, stream->base);
}

flagcxResult_t ecclAdaptorAlltoAll(const void* sendbuff, void* recvbuff, size_t count,
                                   flagcxDataType_t datatype, flagcxInnerComm_t comm,
                                   flagcxStream_t stream) {
    int rank, nranks;
    ecclResult_t res = ecclSuccess;
    res = ecclCommUserRank(comm->base, &rank);
    res = ecclCommCount(comm->base, &nranks);

    size_t size = count * getFlagcxDataTypeSize(datatype);
    const char* buffer_in = static_cast<const char*>(sendbuff);
    char* buffer_out = static_cast<char*>(recvbuff);

    res = ecclGroupStart();
    for (int r = 0; r < nranks; r++) {
        res = ecclSend(static_cast<const void*>(buffer_in + r * size), size, ecclChar, r, comm->base, stream->base);
        res = ecclRecv(static_cast<void*>(buffer_out + r * size), size, ecclChar, r, comm->base, stream->base);
    }
    res = ecclGroupEnd();

    return (flagcxResult_t)res;
}

flagcxResult_t ecclAdaptorSend(const void* sendbuff, size_t count,
                               flagcxDataType_t datatype, int peer,
                               flagcxInnerComm_t comm, flagcxStream_t stream) {
    //TODO: const_cast will be removed in the future
    return (flagcxResult_t)e2f_ret_map[ecclSend(sendbuff, count,
           (ecclDataType_t)f2e_datatype_map[datatype], peer, comm->base, stream->base)];
}

flagcxResult_t ecclAdaptorRecv(void* recvbuff, size_t count,
                               flagcxDataType_t datatype, int peer,
                               flagcxInnerComm_t comm, flagcxStream_t stream) {
    return (flagcxResult_t)e2f_ret_map[ecclRecv(recvbuff, count,
           (ecclDataType_t)f2e_datatype_map[datatype], peer, comm->base, stream->base)];
}

flagcxResult_t ecclAdaptorGroupStart() {
    return (flagcxResult_t)e2f_ret_map[ecclGroupStart()];
}

flagcxResult_t ecclAdaptorGroupEnd() {
    return (flagcxResult_t)e2f_ret_map[ecclGroupEnd()];
}

struct flagcxCCLAdaptor ecclAdaptor = {
  "ECCL",
  // Basic functions
  ecclAdaptorGetVersion,
  ecclAdaptorGetUniqueId,
  ecclAdaptorGetErrorString,
  ecclAdaptorGetLastError,
  // Communicator functions
  ecclAdaptorCommInitRank,
  ecclAdaptorCommFinalize,
  ecclAdaptorCommDestroy,
  ecclAdaptorCommAbort,
  ecclAdaptorCommResume,
  ecclAdaptorCommSuspend,
  ecclAdaptorCommCount,
  ecclAdaptorCommCuDevice,
  ecclAdaptorCommUserRank,
  ecclAdaptorCommGetAsyncError,
  // Communication functions
  ecclAdaptorReduce,
  ecclAdaptorGather,
  ecclAdaptorScatter,
  ecclAdaptorBroadcast,
  ecclAdaptorAllReduce,
  ecclAdaptorReduceScatter,
  ecclAdaptorAllGather,
  ecclAdaptorAlltoAll,
  ecclAdaptorSend,
  ecclAdaptorRecv,
  // Group semantics
  ecclAdaptorGroupStart,
  ecclAdaptorGroupEnd
};

#endif // USE_CAMBRICON_ADAPTOR

#ifndef FLAGCX_H_
#define FLAGCX_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>

/* Error type */
typedef enum {
  flagcxSuccess = 0,
  flagcxUnhandledDeviceError = 1,
  flagcxSystemError = 2,
  flagcxInternalError = 3,
  flagcxInvalidArgument = 4,
  flagcxInvalidUsage = 5,
  flagcxRemoteError = 6,
  flagcxInProgress = 7,
  flagcxNotSupported = 8,
  flagcxNumResults = 9
} flagcxResult_t;

/* Data types */
typedef enum {
  flagcxInt8 = 0,
  flagcxChar = 0,
  flagcxUint8 = 1,
  flagcxInt32 = 2,
  flagcxInt = 2,
  flagcxUint32 = 3,
  flagcxInt64 = 4,
  flagcxUint64 = 5,
  flagcxFloat16 = 6,
  flagcxHalf = 6,
  flagcxFloat32 = 7,
  flagcxFloat = 7,
  flagcxFloat64 = 8,
  flagcxDouble = 8,
  flagcxBfloat16 = 9,
  flagcxNumTypes = 10
} flagcxDataType_t;

/* Reduction operation selector */
typedef enum { flagcxNumOps_dummy = 5 } flagcxRedOp_dummy_t;
typedef enum {
  flagcxSum = 0,
  flagcxProd = 1,
  flagcxMax = 2,
  flagcxMin = 3,
  flagcxAvg = 4,
  flagcxNumOps = 5,
  flagcxMaxRedOp = 0x7fffffff >> (32 - 8 * sizeof(flagcxRedOp_dummy_t))
} flagcxRedOp_t;

size_t getFlagcxDataTypeSize(flagcxDataType_t dtype);

typedef enum {
  flagcxMemcpyHostToDevice = 0,
  flagcxMemcpyDeviceToHost = 1,
  flagcxMemcpyDeviceToDevice = 2
} flagcxMemcpyType_t;

typedef enum {
  flagcxMemHost = 0,
  flagcxMemDevice = 1,
  flagcxMemManaged = 2
} flagcxMemType_t;

#define FLAGCX_UNIQUE_ID_BYTES 256
typedef struct {
  char internal[FLAGCX_UNIQUE_ID_BYTES];
} flagcxUniqueId;
typedef flagcxUniqueId *flagcxUniqueId_t;

/* Opaque handle to flagcxComm */
typedef struct flagcxComm *flagcxComm_t;
/* Opaque handle to flagcxStream */
typedef struct flagcxStream *flagcxStream_t;
/* Opaque handle to flagcxEvent */
typedef struct flagcxEvent *flagcxEvent_t;

struct flagcxDeviceHandle {
  // Basic functions
  flagcxResult_t (*deviceSynchronize)();
  flagcxResult_t (*deviceMemcpy)(void *dst, void *src, size_t size,
                                 flagcxMemcpyType_t type,
                                 flagcxStream_t stream);
  flagcxResult_t (*deviceMemset)(void *ptr, int value, size_t size,
                                 flagcxMemType_t type, flagcxStream_t stream);
  flagcxResult_t (*deviceMalloc)(void **ptr, size_t size, flagcxMemType_t type,
                                 flagcxStream_t stream);
  flagcxResult_t (*deviceFree)(void *ptr, flagcxMemType_t type,
                               flagcxStream_t stream);
  flagcxResult_t (*setDevice)(int dev);
  flagcxResult_t (*getDevice)(int *dev);
  flagcxResult_t (*getDeviceCount)(int *count);
  flagcxResult_t (*getVendor)(char *vendor);
  // Stream functions
  flagcxResult_t (*streamCreate)(flagcxStream_t *stream);
  flagcxResult_t (*streamDestroy)(flagcxStream_t stream);
  flagcxResult_t (*streamCopy)(flagcxStream_t *newStream, void *oldStream);
  flagcxResult_t (*streamFree)(flagcxStream_t stream);
  flagcxResult_t (*streamSynchronize)(flagcxStream_t stream);
  flagcxResult_t (*streamQuery)(flagcxStream_t stream);
  flagcxResult_t (*streamWaitEvent)(flagcxStream_t stream, flagcxEvent_t event);
  // Event functions
  flagcxResult_t (*eventCreate)(flagcxEvent_t *event);
  flagcxResult_t (*eventDestroy)(flagcxEvent_t event);
  flagcxResult_t (*eventRecord)(flagcxEvent_t event, flagcxStream_t stream);
  flagcxResult_t (*eventSynchronize)(flagcxEvent_t event);
  flagcxResult_t (*eventQuery)(flagcxEvent_t event);
};
typedef struct flagcxDeviceHandle *flagcxDeviceHandle_t;

struct flagcxHandlerGroup {
  flagcxUniqueId_t uniqueId;
  flagcxComm_t comm;
  flagcxDeviceHandle_t devHandle;
};
typedef struct flagcxHandlerGroup *flagcxHandlerGroup_t;

/* Init and free FlagCX handls including flagcxComm_t, flagcxStream_t */
flagcxResult_t flagcxHandleInit(flagcxHandlerGroup_t *handler);

flagcxResult_t flagcxHandleFree(flagcxHandlerGroup_t handler);

/* Check if the FlagCX communicator type is homogeneous or heterogeneous */
flagcxResult_t flagcxIsHomoComm(flagcxComm_t comm, int *isHomo);

/* Return the version of the FlagCX library in the supplied integer.
 * It contains the underlying adaptor library version and FlagCX core version
 */
flagcxResult_t flagcxGetVersion(int *version);

/* Generates an Id to be used in flagcxCommInitRank. flagcxGetUniqueId should be
 * called once and the Id should be distributed to all ranks in the
 * communicator before calling flagcxCommInitRank. */
flagcxResult_t flagcxGetUniqueId(flagcxUniqueId_t *uniqueId);

/* Creates a new communicator (multi thread/process version).
 * rank must be between 0 and nranks-1 and unique within a communicator clique.
 * Each rank is associated to a device, which has to be set before calling
 * flagcxCommInitRank. flagcxCommInitRank implicitly syncronizes with other
 * ranks, so it must be called by different threads/processes or use
 * flagcxGroupStart/flagcxGroupEnd. */
flagcxResult_t flagcxCommInitRank(flagcxComm_t *comm, int nranks,
                                  flagcxUniqueId_t commId, int rank);

/* Finalize a communicator. flagcxCommFinalize flushes all issued
 * communications, and marks communicator state as flagcxInProgress. The state
 * will change to flagcxSuccess when the communicator is globally quiescent and
 * related resources are freed; then, calling flagcxCommDestroy can locally free
 * the rest of the resources (e.g. communicator itself) without blocking. */
flagcxResult_t flagcxCommFinalize(flagcxComm_t comm);

/* Frees local resources associated with communicator object. */
flagcxResult_t flagcxCommDestroy(flagcxComm_t comm);

/* Frees resources associated with communicator object and aborts any operations
 * that might still be running on the device. */
flagcxResult_t flagcxCommAbort(flagcxComm_t comm);

/* Resume a communicator. */
flagcxResult_t flagcxCommResume(flagcxComm_t comm);

/* Suspend a communicator. */
flagcxResult_t flagcxCommSuspend(flagcxComm_t comm);

/* Returns a string for each error code. */
const char *flagcxGetErrorString(flagcxResult_t result);

/* Returns a human-readable message of the last error that occurred. */
const char *flagcxGetLastError(flagcxComm_t comm);

/* Checks whether the comm has encountered any asynchronous errors */
flagcxResult_t flagcxCommGetAsyncError(flagcxComm_t comm,
                                       flagcxResult_t asyncError);

/* Gets the number of ranks in the communicator clique. */
flagcxResult_t flagcxCommCount(const flagcxComm_t comm, int *count);

/* Returns the device number associated with the communicator. */
flagcxResult_t flagcxCommGetDeviceNumber(const flagcxComm_t comm, int *device);

/* Returns the user-ordered "rank" associated with the communicator. */
flagcxResult_t flagcxCommUserRank(const flagcxComm_t comm, int *rank);

/*
 * Collective communication operations
 *
 * Collective communication operations must be called separately for each
 * communicator in a communicator clique.
 *
 * They return when operations have been enqueued on the FlagCX stream.
 *
 * Since they may perform inter-CPU synchronization, each call has to be done
 * from a different thread or process, or need to use Group Semantics (see
 * below).
 */

/*
 * Barrier
 *
 * Blocks until all processes in the communicator have reached this routine.
 *
 */
flagcxResult_t flagcxBarrier(flagcxComm_t comm, flagcxStream_t stream);

/*
 * Reduce
 *
 * Reduces data arrays of length count in sendbuff into recvbuff using op
 * operation.
 * recvbuff may be NULL on all calls except for root device.
 * root is the rank (not the device) where data will reside after the
 * operation is complete.
 *
 * In-place operation will happen if sendbuff == recvbuff.
 */
flagcxResult_t flagcxReduce(const void *sendbuff, void *recvbuff, size_t count,
                            flagcxDataType_t datatype, flagcxRedOp_t op,
                            int root, flagcxComm_t comm, flagcxStream_t stream);

/*
 * Gather
 *
 * Gathers data arrays of length count in sendbuff into recvbuff.
 * recvbuff may bu NULL on all calls except root device.
 * root is the rank (not the device) where data will reside after the
 * operation is complete.
 *
 * In-place operations will happen if sendbuff == recvbuff + rank * count.
 */
flagcxResult_t flagcxGather(const void *sendbuff, void *recvbuff, size_t count,
                            flagcxDataType_t datatype, int root,
                            flagcxComm_t comm, flagcxStream_t stream);

/*
 * Scatter
 *
 * Scatters data arrays of sendcount in sendbuff into recvbuff.
 * sendbuff may bu NULL on all calls except root device.
 * root is the rank (not the device) where data will reside before the
 * operation is started.
 *
 * In-place operations will happen if sendbuff + rank * count == recvbuff.
 */
flagcxResult_t flagcxScatter(const void *sendbuff, void *recvbuff, size_t count,
                             flagcxDataType_t datatype, int root,
                             flagcxComm_t comm, flagcxStream_t stream);

/*
 * Broadcast
 *
 * Copies count values from root to all other devices.
 * root is the rank (not the device) where data resides before the
 * operation is started.
 *
 * In-place operation will happen if sendbuff == recvbuff.
 */
flagcxResult_t flagcxBroadcast(const void *sendbuff, void *recvbuff,
                               size_t count, flagcxDataType_t datatype,
                               int root, flagcxComm_t comm,
                               flagcxStream_t stream);

/*
 * All-Reduce
 *
 * Reduces data arrays of length count in sendbuff using op operation, and
 * leaves identical copies of result on each recvbuff.
 *
 * In-place operation will happen if sendbuff == recvbuff.
 */
flagcxResult_t flagcxAllReduce(const void *sendbuff, void *recvbuff,
                               size_t count, flagcxDataType_t datatype,
                               flagcxRedOp_t op, flagcxComm_t comm,
                               flagcxStream_t stream);

/*
 * Reduce-Scatter
 *
 * Reduces data in sendbuff using op operation and leaves reduced result
 * scattered over the devices so that recvbuff on rank i will contain the i-th
 * block of the result.
 * Assumes sendcount is equal to nranks*recvcount, which means that sendbuff
 * should have a size of at least nranks*recvcount elements.
 *
 * In-place operations will happen if recvbuff == sendbuff + rank * recvcount.
 */
flagcxResult_t flagcxReduceScatter(const void *sendbuff, void *recvbuff,
                                   size_t recvcount, flagcxDataType_t datatype,
                                   flagcxRedOp_t op, flagcxComm_t comm,
                                   flagcxStream_t stream);

/*
 * All-Gather
 *
 * Each device gathers sendcount values from other APUs into recvbuff,
 * receiving data from rank i at offset i*sendcount.
 * Assumes recvcount is equal to nranks*sendcount, which means that recvbuff
 * should have a size of at least nranks*sendcount elements.
 *
 * In-place operations will happen if sendbuff == recvbuff + rank * sendcount.
 */
flagcxResult_t flagcxAllGather(const void *sendbuff, void *recvbuff,
                               size_t sendcount, flagcxDataType_t datatype,
                               flagcxComm_t comm, flagcxStream_t stream);

/*
 * All-to-all
 *
 * Each device sends count values to other APUs into recvbuffer,
 * receiving count values from other APUs into sendbuffer.
 *
 * In-place operations will happen if sendbuff == recvbuff.
 */
flagcxResult_t flagcxAlltoAll(const void *sendbuff, void *recvbuff,
                              size_t count, flagcxDataType_t datatype,
                              flagcxComm_t comm, flagcxStream_t stream);

/*
 * All-to-allv
 *
 * Each device may send different count values to other APUs into recvbuffer,
 * receiving different count values from other APUs into sendbuffer.
 *
 * In-place operations will happen if sendbuff == recvbuff.
 */
flagcxResult_t flagcxAlltoAllv(const void *sendbuff, size_t *sendcounts,
                               size_t *sdispls, void *recvbuff,
                               size_t *recvcounts, size_t *rdispls,
                               flagcxDataType_t datatype, flagcxComm_t comm,
                               flagcxStream_t stream);

/*
 * Send
 *
 * Send data from sendbuff to rank peer.
 *
 * Rank peer needs to call flagcxRecv with the same datatype and the same count
 * from this rank.
 *
 * This operation is blocking for the GPU. If multiple flagcxSend and flagcxRecv
 * operations need to progress concurrently to complete, they must be fused
 * within a flagcxGroupStart/ flagcxGroupEnd section.
 */
flagcxResult_t flagcxSend(const void *sendbuff, size_t count,
                          flagcxDataType_t datatype, int peer,
                          flagcxComm_t comm, flagcxStream_t stream);

/*
 * Receive
 *
 * Receive data from rank peer into recvbuff.
 *
 * Rank peer needs to call flagcxSend with the same datatype and the same count
 * to this rank.
 *
 * This operation is blocking for the GPU. If multiple flagcxSend and flagcxRecv
 * operations need to progress concurrently to complete, they must be fused
 * within a flagcxGroupStart/ flagcxGroupEnd section.
 */
flagcxResult_t flagcxRecv(void *recvbuff, size_t count,
                          flagcxDataType_t datatype, int peer,
                          flagcxComm_t comm, flagcxStream_t stream);

/*
 * Group semantics
 *
 * When managing multiple APUs from a single thread, and since FLAGCX collective
 * calls may perform inter-CPU synchronization, we need to "group" calls for
 * different ranks/devices into a single call.
 *
 * Grouping FLAGCX calls as being part of the same collective operation is done
 * using flagcxGroupStart and flagcxGroupEnd. flagcxGroupStart will enqueue all
 * collective calls until the flagcxGroupEnd call, which will wait for all calls
 * to be complete. Note that for collective communication, flagcxGroupEnd only
 * guarantees that the operations are enqueued on the FlagCX streams, not that
 * the operation is effectively done.
 *
 * Both collective communication and flagcxCommInitRank can be used in
 * conjunction of flagcxGroupStart/flagcxGroupEnd, but not together.
 *
 * Group semantics also allow to fuse multiple operations on the same device
 * to improve performance (for aggregated collective calls), or to permit
 * concurrent progress of multiple send/receive operations.
 */

/*
 * Group Start
 *
 * Start a group call. All calls to FLAGCX until flagcxGroupEnd will be fused
 * into a single FLAGCX operation. Nothing will be started on the FlagCX stream
 * until flagcxGroupEnd.
 */
flagcxResult_t flagcxGroupStart(flagcxComm_t comm);

/*
 * Group End
 *
 * End a group call. Start a fused FLAGCX operation consisting of all calls
 * since flagcxGroupStart. Operations on the FlagCX stream depending on the
 * FLAGCX operations need to be called after flagcxGroupEnd.
 */
flagcxResult_t flagcxGroupEnd(flagcxComm_t comm);

#ifdef __cplusplus
} // end extern "C"
#endif

#endif // end include guard
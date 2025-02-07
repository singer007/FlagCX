#include "enflame_adaptor.h"

#ifdef USE_ENFLAME_ADAPTOR

std::map<flagcxMemcpyType_t, topsMemcpyKind> memcpy_type_map = {
    {flagcxMemcpyHostToDevice, topsMemcpyHostToDevice},
    {flagcxMemcpyDeviceToHost, topsMemcpyDeviceToHost},
    {flagcxMemcpyDeviceToDevice, topsMemcpyDeviceToDevice},
};

flagcxResult_t gcuAdaptorDeviceSynchronize() {
    DEVCHECK(topsDeviceSynchronize());
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorDeviceMemcpy(void *dst, void *src, size_t size, flagcxMemcpyType_t type, flagcxStream_t stream, void *args) {
    if (stream == NULL) {
        DEVCHECK(topsMemcpy(dst, src, size, memcpy_type_map[type]));
    } else {
        DEVCHECK(topsMemcpyAsync(dst, src, size, memcpy_type_map[type], stream->base));
    }
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorDeviceMemset(void *ptr, int value, size_t size, flagcxMemType_t type, flagcxStream_t stream) {
    if (type == flagcxMemHost) {
        memset(ptr, value, size);
    } else {
        if (stream == NULL) {
            DEVCHECK(topsMemset(ptr, value, size));
        } else {
            DEVCHECK(topsMemsetAsync(ptr, value, size, stream->base));
        }
    }
    return flagcxSuccess;
}

//TODO: unsupported
flagcxResult_t gcuAdaptorDeviceMalloc(void **ptr, size_t size, flagcxMemType_t type) {
    if (type == flagcxMemHost) {
        DEVCHECK(topsHostMalloc(ptr, size));
    } else if (type == flagcxMemDevice) {
        DEVCHECK(topsMalloc(ptr, size));
    } else if (type == flagcxMemManaged) {
        //DEVCHECK(topsMallocManaged(ptr, size, topsMemAttachGlobal));
        DEVCHECK(topsErrorNotSupported);
    }
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorDeviceFree(void *ptr, flagcxMemType_t type) {
    if (type == flagcxMemHost) {
        DEVCHECK(topsHostFree(ptr));
    } else {
        DEVCHECK(topsFree(ptr));
    }
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorSetDevice(int dev) {
    DEVCHECK(topsSetDevice(dev));
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorGetDevice(int *dev) {
    DEVCHECK(topsGetDevice(dev));
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorGetDeviceCount(int *count) {
    DEVCHECK(topsGetDeviceCount(count));
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorGetVendor(char *vendor) {
    strcpy(vendor, "ENFLAME");
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorGdrMemAlloc(void **ptr, size_t size, void *memHandle) {
    if (ptr == NULL) {
        return flagcxInvalidArgument;
    }
    DEVCHECK(topsMalloc(ptr, size));
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorGdrMemFree(void *ptr, void *memHandle) {
    if (ptr == NULL) {
        return flagcxSuccess;
    }
    DEVCHECK(topsFree(ptr));
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorStreamCreate(flagcxStream_t *stream) {
    (*stream) = NULL;
    flagcxCalloc(stream, 1);
    DEVCHECK(topsStreamCreateWithFlags((topsStream_t *)(*stream), topsStreamNonBlocking));
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorStreamDestroy(flagcxStream_t stream) {
    if (stream != NULL) {
        DEVCHECK(topsStreamDestroy(stream->base));
        free(stream);
        stream = NULL;
    }
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorStreamSynchronize(flagcxStream_t stream) {
    if (stream != NULL) {
        DEVCHECK(topsStreamSynchronize(stream->base));
    }
    return flagcxSuccess;
}

flagcxResult_t gcuAdaptorStreamQuery(flagcxStream_t stream) {
    flagcxResult_t res = flagcxSuccess;
    if (stream != NULL) {
        topsError_t error = topsStreamQuery(stream->base);
        if(error == topsSuccess) {
            res = flagcxSuccess;
        } else if (error == topsErrorNotReady) {
            res = flagcxInProgress;
        } else {
            res = flagcxUnhandledDeviceError;
        }
    }
    return res;
}

//TODO: unsupported
flagcxResult_t gcuAdaptorLaunchHostFunc(flagcxStream_t stream, void (*fn)(void *),  void *args) {
    // if (stream != NULL) {
    //     DEVCHECK(topsLaunchHostFunc(stream->base, fn, args));
    // }
    // return flagcxSuccess;
    return flagcxNotSupported;
}

struct flagcxDeviceAdaptor gcuAdaptor {
   "GCU",
   // Basic functions
   gcuAdaptorDeviceSynchronize,
   gcuAdaptorDeviceMemcpy,
   gcuAdaptorDeviceMemset,
   gcuAdaptorDeviceMalloc,
   gcuAdaptorDeviceFree,
   gcuAdaptorSetDevice,
   gcuAdaptorGetDevice,
   gcuAdaptorGetDeviceCount,
   gcuAdaptorGetVendor,
   // GDR functions
   NULL, // flagcxResult_t (*memHandleInit)(int dev_id, void **memHandle);
   NULL, // flagcxResult_t (*memHandleDestroy)(int dev, void *memHandle);
   gcuAdaptorGdrMemAlloc,
   gcuAdaptorGdrMemFree,
   NULL, // flagcxResult_t (*hostShareMemAlloc)(void **ptr, size_t size, void *memHandle);
   NULL, // flagcxResult_t (*hostShareMemFree)(void *ptr, void *memHandle);
   // Stream functions
   gcuAdaptorStreamCreate,
   gcuAdaptorStreamDestroy,
   gcuAdaptorStreamSynchronize,
   gcuAdaptorStreamQuery,
   // Kernel launch
   NULL, // flagcxResult_t (*launchKernel)(void *func, unsigned int block_x, unsigned int block_y, unsigned int block_z, unsigned int grid_x, unsigned int grid_y, unsigned int grid_z, void **args, size_t share_mem, void *stream, void *memHandle);
   NULL, // flagcxResult_t (*copyArgsInit)(void **args);
   NULL, // flagcxResult_t (*copyArgsFree)(void *args);
   // Others
   NULL, // flagcxResult_t (*topoGetSystem)(void *topoArgs, void **system);
   gcuAdaptorLaunchHostFunc
};

#endif // USE_ENFLAME_ADAPTOR

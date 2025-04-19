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



flagcxResult_t gcuAdaptorDeviceMalloc(void **ptr, size_t size,
                                       flagcxMemType_t type,
                                       flagcxStream_t stream) {
  if (type == flagcxMemHost) {
    DEVCHECK(topsHostMalloc(ptr, size));
  } else if (type == flagcxMemDevice) {
    if (stream == NULL) {
      DEVCHECK(topsMalloc(ptr, size));
    } else {
      DEVCHECK(topsMallocAsync(ptr, size, stream->base, topsDeviceMallocDefault));
    }
  } else if (type == flagcxMemManaged) {
    //DEVCHECK(cudaMallocManaged(ptr, size, cudaMemAttachGlobal));
    DEVCHECK(topsErrorNotSupported);
  }
  return flagcxSuccess;
}

flagcxResult_t gcuAdaptorDeviceFree(void *ptr, flagcxMemType_t type,
                                     flagcxStream_t stream) {
  if (type == flagcxMemHost) {
    DEVCHECK(topsHostFree(ptr));
  } else if (type == flagcxMemDevice) {
    if (stream == NULL) {
      DEVCHECK(topsFree(ptr));
    } else {
      DEVCHECK(topsFreeAsync(ptr, stream->base));
    }
  } else if (type == flagcxMemManaged) {
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
    printf("%s:%d before get GetDeviceCount", __func__, __LINE__);
    DEVCHECK(topsGetDeviceCount(count));
    printf("%s:%d after get GetDeviceCount", __func__, __LINE__);
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
    DEVCHECK(topsExtMallocWithFlags(ptr, size, topsMallocHostAccessable));
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

flagcxResult_t gcuAdaptorStreamCopy(flagcxStream_t *newStream,
                                     void *oldStream) {
  (*newStream) = NULL;
  flagcxCalloc(newStream, 1);
  memcpy((void *)*newStream, oldStream, sizeof(topsStream_t));
  return flagcxSuccess;
}

flagcxResult_t gcuAdaptorStreamFree(flagcxStream_t stream) {
  if (stream != NULL) {
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

flagcxResult_t gcuAdaptorStreamWaitEvent(flagcxStream_t stream,
                                          flagcxEvent_t event) {
  if (stream != NULL && event != NULL) {
    DEVCHECK(
        //topsStreamWaitEvent(stream->base, event->base, topsEventWaitDefault));
        topsStreamWaitEvent(stream->base, event->base, topsEventDefault));
  }
  return flagcxSuccess;
}

flagcxResult_t gcuAdaptorEventCreate(flagcxEvent_t *event) {
  (*event) = NULL;
  flagcxCalloc(event, 1);
  DEVCHECK(topsEventCreateWithFlags((topsEvent_t *)(*event),
                                    topsEventDisableTiming));
  return flagcxSuccess;
}

flagcxResult_t gcuAdaptorEventDestroy(flagcxEvent_t event) {
  if (event != NULL) {
    DEVCHECK(topsEventDestroy(event->base));
    free(event);
    event = NULL;
  }
  return flagcxSuccess;
}

flagcxResult_t gcuAdaptorEventRecord(flagcxEvent_t event,
                                      flagcxStream_t stream) {
  if (event != NULL) {
    if (stream != NULL) {
      // DEVCHECK(topsEventRecordWithFlags(event->base, stream->base,
      //                                   topsEventRecordDefault));
      DEVCHECK(topsEventRecord(event->base, stream->base));
    } else {
      DEVCHECK(topsEventRecord(event->base));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t gcuAdaptorEventSynchronize(flagcxEvent_t event) {
  if (event != NULL) {
    DEVCHECK(topsEventSynchronize(event->base));
  }
  return flagcxSuccess;
}

flagcxResult_t gcuAdaptorEventQuery(flagcxEvent_t event) {
  flagcxResult_t res = flagcxSuccess;
  if (event != NULL) {
    topsError_t error = topsEventQuery(event->base);
    if (error == topsSuccess) {
      res = flagcxSuccess;
    } else if (error == topsErrorNotReady) {
      res = flagcxInProgress;
    } else {
      res = flagcxUnhandledDeviceError;
    }
  }
  return res;
}

struct LaunchConfig {
  void (*fn)(void *);
  void *args;

  topsEvent_t event;
};

void topsHostFunc(topsStream_t stream, topsError_t status, void* userData) {
  struct LaunchConfig* pData = (struct LaunchConfig*)userData;

  //pData->fn(pData->args);
  pthread_t thread;
  pthread_create(&thread, NULL, (void* (*)(void*))pData->fn, (void *)pData->args);
  //thread.detach();

  auto ret = (topsEventRecord(pData->event, stream));
  (void)ret;
}

void topsEventReleaseFunc(topsStream_t stream, topsError_t status, void* userData) {
  topsEvent_t event = (topsEvent_t)userData;
  auto ret = (topsEventDestroy(event));
  (void)ret;
}

//TODO: unsupported
flagcxResult_t gcuAdaptorLaunchHostFunc(flagcxStream_t stream, void (*fn)(void *),  void *args) {
    if (stream != NULL) {
        topsEvent_t event;
        DEVCHECK(topsEventCreateWithFlags(&event, topsEventStrongOrder));
        struct LaunchConfig* userData = (struct LaunchConfig*)malloc(sizeof(struct LaunchConfig));
        userData->fn = fn;
        userData->args = args;
        userData->event = event;

        DEVCHECK(topsStreamAddCallback(stream->base, (topsStreamCallback_t)topsHostFunc, userData, topsStreamCallbackBlocking));
        DEVCHECK(topsStreamWaitEvent(stream->base, event, 0));
        DEVCHECK(topsStreamAddCallback(stream->base, (topsStreamCallback_t)topsEventReleaseFunc, (void*)event, topsStreamCallbackBlocking));
    }
    return flagcxSuccess;
    //return flagcxNotSupported;
}

flagcxResult_t gcuAdaptorGetDeviceProperties(struct flagcxDevProps *props,
                                              int dev) {
  if (props == NULL) {
    return flagcxInvalidArgument;
  }

  topsDeviceProp_t devProp;
  DEVCHECK(topsGetDeviceProperties(&devProp, dev));
  strncpy(props->name, devProp.name, sizeof(props->name) - 1);
  props->name[sizeof(props->name) - 1] = '\0';
  props->pciBusId = devProp.pciBusID;
  props->pciDeviceId = devProp.pciDeviceID;
  props->pciDomainId = devProp.pciDomainID;
  // TODO: see if there's another way to get this info. In some cuda versions,
  // cudaDeviceProp does not have `gpuDirectRDMASupported` field
  // props->gdrSupported = devProp.gpuDirectRDMASupported;

  return flagcxSuccess;
}

flagcxResult_t gcuAdaptorGetDevicePciBusId(char *pciBusId, int len, int dev) {
  if (pciBusId == NULL) {
    return flagcxInvalidArgument;
  }
  DEVCHECK(topsDeviceGetPCIBusId(pciBusId, len, dev));
  return flagcxSuccess;
}

flagcxResult_t gcuAdaptorGetDeviceByPciBusId(int *dev, const char *pciBusId) {
  if (dev == NULL || pciBusId == NULL) {
    return flagcxInvalidArgument;
  }
  DEVCHECK(topsDeviceGetByPCIBusId(dev, pciBusId));
  return flagcxSuccess;
}

struct flagcxDeviceAdaptor gcuAdaptor {
  "GCU",
      // Basic functions
      gcuAdaptorDeviceSynchronize, gcuAdaptorDeviceMemcpy,
      gcuAdaptorDeviceMemset, gcuAdaptorDeviceMalloc, gcuAdaptorDeviceFree,
      gcuAdaptorSetDevice, gcuAdaptorGetDevice, gcuAdaptorGetDeviceCount,
      gcuAdaptorGetVendor,
      // GDR functions
      NULL, // flagcxResult_t (*memHandleInit)(int dev_id, void **memHandle);
      NULL, // flagcxResult_t (*memHandleDestroy)(int dev, void *memHandle);
      gcuAdaptorGdrMemAlloc, gcuAdaptorGdrMemFree,
      NULL, // flagcxResult_t (*hostShareMemAlloc)(void **ptr, size_t size, void
            // *memHandle);
      NULL, // flagcxResult_t (*hostShareMemFree)(void *ptr, void *memHandle);
      // Stream functions
      gcuAdaptorStreamCreate, gcuAdaptorStreamDestroy, gcuAdaptorStreamCopy,
      gcuAdaptorStreamFree, gcuAdaptorStreamSynchronize,
      gcuAdaptorStreamQuery, gcuAdaptorStreamWaitEvent,
      // Event functions
      gcuAdaptorEventCreate, gcuAdaptorEventDestroy, gcuAdaptorEventRecord,
      gcuAdaptorEventSynchronize, gcuAdaptorEventQuery,
      // Kernel launch
      NULL, // flagcxResult_t (*launchKernel)(void *func, unsigned int block_x,
            // unsigned int block_y, unsigned int block_z, unsigned int grid_x,
            // unsigned int grid_y, unsigned int grid_z, void **args, size_t
            // share_mem, void *stream, void *memHandle);
      NULL, // flagcxResult_t (*copyArgsInit)(void **args);
      NULL, // flagcxResult_t (*copyArgsFree)(void *args);
      // Others
      gcuAdaptorGetDeviceProperties, // flagcxResult_t
                                      // (*getDeviceProperties)(struct
                                      // flagcxDevProps *props, int dev);
      gcuAdaptorGetDevicePciBusId, // flagcxResult_t (*getDevicePciBusId)(char
                                    // *pciBusId, int len, int dev);
      gcuAdaptorGetDeviceByPciBusId, // flagcxResult_t
                                      // (*getDeviceByPciBusId)(int
                                      // *dev, const char *pciBusId);
      gcuAdaptorLaunchHostFunc
};

#endif // USE_ENFLAME_ADAPTOR

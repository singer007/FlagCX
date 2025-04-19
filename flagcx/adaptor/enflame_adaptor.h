#ifdef USE_ENFLAME_ADAPTOR

#include "eccl.h"
#include "tops/tops_runtime_api.h"
#include "flagcx.h"
#include "comm.h"
#include "alloc.h"
#include "adaptor.h"
#include <map>

struct flagcxInnerComm {
    ecclComm_t base;
};

struct flagcxStream {
    topsStream_t base;
};

struct flagcxEvent {
  topsEvent_t base;
};

#define DEVCHECK(func) {                                         \
   int ret = func;                                               \
   if(ret != topsSuccess) return flagcxUnhandledDeviceError;     \
}

#endif // USE_ENFLAME_ADAPTOR

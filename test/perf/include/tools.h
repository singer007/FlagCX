#include <cstdint>
#include "flagcx.h"

class timer {
public:
  timer();
  double elapsed() const;
  double reset();

  std::uint64_t start;
};

class parser {
public:
  parser(int argc, char **argv);
  size_t getMinBytes() const { return minBytes; }
  size_t getMaxBytes() const { return maxBytes; }
  size_t getStepFactor() const { return stepFactor; }
  int getWarmupIters() const { return warmupIters; }
  int getTestIters() const { return testIters; }
  bool isPrintBuffer() const { return printBuffer == 1; }
  int getRootRank() const { return root; }

  size_t minBytes;
  size_t maxBytes;
  int stepFactor;
  int warmupIters;
  int testIters; 
  int printBuffer;
  int root;
};


#define CHECK_NZ_EXIT(status, ...)                                                          \
    do {                                                                                       \
        if (status != flagcxSuccess) {                                           \
            fprintf(stderr, "%s:%d: non-zero status: %d: %s, exiting... ", __FILE__, __LINE__, \
                    status, strerror(errno));                                                  \
            fprintf(stderr, __VA_ARGS__);                                                      \
            fprintf(stderr, "\n");                                                             \
            exit(-1);                                                                          \
        }                                                                                      \
    } while (0)

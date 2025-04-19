/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "topo.h"
#include "bootstrap.h"
#include "comm.h"
#include "core.h"
#include "cpuset.h"
#include "graph.h"
#include "net.h"
#include "transport.h"
#include "xml.h"
#include <fcntl.h>

#define BUSID_SIZE (sizeof("0000:00:00.0"))
#define BUSID_REDUCED_SIZE (sizeof("0000:00"))

const char *topoNodeTypeStr[] = {"APU", "PCI", "CCI", "CPU",
                                 "NIC", "NET", "HBD"};
const char *topoLinkTypeStr[] = {"LOC", "CCI", "",    "PCI", "",
                                 "",    "",    "SYS", "NET"};
const char *topoPathTypeStr[] = {"LOC", "CCI", "CCB", "PIX", "PXB",
                                 "PXN", "PHB", "SYS", "NET", "DIS"};

struct kvDict kvDictPciGen[] = {{"2.5 GT/s", 15},
                                {"5 GT/s", 30},
                                {"8 GT/s", 60},
                                {"16 GT/s", 120},
                                {"32 GT/s", 240}, /* Kernel 5.6 and earlier */
                                {"2.5 GT/s PCIe", 15},
                                {"5.0 GT/s PCIe", 30},
                                {"8.0 GT/s PCIe", 60},
                                {"16.0 GT/s PCIe", 120},
                                {"32.0 GT/s PCIe", 240},
                                {"64.0 GT/s PCIe", 480},
                                {NULL, 60 /* Default fallback */}};

flagcxResult_t flagcxTopoGetLocal(struct flagcxTopoServer *topoServer, int type,
                                  int index, int resultType, int **locals,
                                  int *localCount, int *pathType) {
  int minType = PATH_DIS;
  float maxBw = 0;
  int count = 0;
  FLAGCXCHECK(flagcxCalloc(locals, topoServer->nodes[resultType].count));
  struct flagcxTopoPath *paths =
      topoServer->nodes[type].nodes[index].paths[resultType];

  for (int i = 0; i < topoServer->nodes[resultType].count; i++) {
    if (paths[i].bw > maxBw ||
        (paths[i].bw == maxBw && paths[i].type < minType)) {
      maxBw = paths[i].bw;
      minType = paths[i].type;
      if (pathType)
        *pathType = minType;
      count = 0;
    }
    if (paths[i].bw == maxBw && paths[i].type == minType)
      (*locals)[count++] = i;
  }
  *localCount = count;
  return flagcxSuccess;
}

static flagcxResult_t flagcxTopoGetInterCpuBw(struct flagcxTopoNode *cpu,
                                              float *bw) {
  *bw = LOC_BW;
  if (cpu->cpu.arch == FLAGCX_TOPO_CPU_ARCH_POWER) {
    *bw = P9_BW;
    return flagcxSuccess;
  }
  if (cpu->cpu.arch == FLAGCX_TOPO_CPU_ARCH_ARM) {
    *bw = ARM_BW;
    return flagcxSuccess;
  }
  if (cpu->cpu.arch == FLAGCX_TOPO_CPU_ARCH_X86 &&
      cpu->cpu.vendor == FLAGCX_TOPO_CPU_VENDOR_INTEL) {
    *bw = cpu->cpu.model == FLAGCX_TOPO_CPU_TYPE_SKL ? SKL_QPI_BW : QPI_BW;
  }
  if (cpu->cpu.arch == FLAGCX_TOPO_CPU_ARCH_X86 &&
      cpu->cpu.vendor == FLAGCX_TOPO_CPU_VENDOR_AMD) {
    *bw = AMD_BW;
  }
  if (cpu->cpu.arch == FLAGCX_TOPO_CPU_ARCH_X86 &&
      cpu->cpu.vendor == FLAGCX_TOPO_CPU_VENDOR_ZHAOXIN) {
    *bw = cpu->cpu.model == FLAGCX_TOPO_CPU_TYPE_YONGFENG ? YONGFENG_ZPI_BW
                                                          : ZPI_BW;
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoGetNode(struct flagcxTopoServer *topoServer,
                                 struct flagcxTopoNode **node, int type,
                                 uint64_t id) {
  for (int i = 0; i < topoServer->nodes[type].count; i++) {
    if (topoServer->nodes[type].nodes[i].id == id) {
      *node = topoServer->nodes[type].nodes + i;
      return flagcxSuccess;
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoCreateNode(struct flagcxTopoServer *topoServer,
                                    struct flagcxTopoNode **node, int type,
                                    uint64_t id) {
  if (topoServer->nodes[type].count == FLAGCX_TOPO_MAX_NODES) {
    WARN("Error : tried to create too many nodes of type %d", type);
    return flagcxInternalError;
  }
  struct flagcxTopoNode *tempNode =
      topoServer->nodes[type].nodes + topoServer->nodes[type].count;
  topoServer->nodes[type].count++;
  tempNode->type = type;
  tempNode->id = id;
  if (type == APU) {
    tempNode->nlinks = 1;
    tempNode->links[0].type = LINK_LOC;
    tempNode->links[0].remNode = tempNode;
    tempNode->links[0].bw = LOC_BW; // TODO: local bw of different APUs might
                                    // differ, change this in the future
    tempNode->apu.dev = FLAGCX_TOPO_UNDEF;
    tempNode->apu.rank = FLAGCX_TOPO_UNDEF;
  } else if (type == CPU) {
    tempNode->cpu.arch = FLAGCX_TOPO_UNDEF;
    tempNode->cpu.vendor = FLAGCX_TOPO_UNDEF;
    tempNode->cpu.model = FLAGCX_TOPO_UNDEF;
  } else if (type == NET) {
    tempNode->net.asic = 0ULL;
    tempNode->net.port = FLAGCX_TOPO_UNDEF;
    tempNode->net.bw = 0.0;
    tempNode->net.latency = 0.0;
  }
  *node = tempNode;
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoConnectNodes(struct flagcxTopoNode *node,
                                      struct flagcxTopoNode *remNode, int type,
                                      float bw) {
  struct flagcxTopoLink *link;
  // check if there's an existing link of this type between node and remNode
  for (link = node->links;
       link - node->links != FLAGCX_TOPO_MAX_LINKS && link->remNode; link++) {
    if (link->remNode == remNode && link->type == type)
      break;
  }
  if (link - node->links == FLAGCX_TOPO_MAX_LINKS) {
    WARN("ERROR: too many topo links (max %d)", FLAGCX_TOPO_MAX_LINKS);
    return flagcxInternalError;
  }
  if (link->remNode == NULL)
    node->nlinks++;
  link->type = type;
  link->remNode = remNode;
  link->bw += bw;
  // TODO: sort links in BW descending order when we have bw info
  return flagcxSuccess;
}

static flagcxResult_t flagcxTopoIdToIndex(struct flagcxTopoServer *topoServer,
                                          int type, int64_t id, int *index) {
  *index = -1;
  for (int i = 0; i < topoServer->nodes[type].count; i++) {
    if (topoServer->nodes[type].nodes[i].id == id) {
      *index = i;
      return flagcxSuccess;
    }
  }
  return flagcxInternalError;
}

flagcxResult_t flagcxTopoRemoveNode(struct flagcxTopoServer *topoServer,
                                    int type, int index) {
  struct flagcxTopoNode *delNode = topoServer->nodes[type].nodes + index;
  for (int t = 0; t < FLAGCX_TOPO_NODE_TYPES; t++) {
    free(delNode->paths[t]);
    for (int n = 0; n < topoServer->nodes[t].count; n++) {
      struct flagcxTopoNode *node = topoServer->nodes[t].nodes + n;
      if (node == delNode)
        continue;
      for (int l = 0; l < node->nlinks; l++) {
        while (l < node->nlinks && node->links[l].remNode == delNode) {
          memmove(node->links + l, node->links + l + 1,
                  (node->nlinks - l - 1) * sizeof(struct flagcxTopoLink));
          node->nlinks--;
        }
        if (l < node->nlinks && node->links[l].remNode->type == type &&
            node->links[l].remNode >= delNode) {
          node->links[l].remNode--;
        }
      }
    }
  }
  memmove(delNode, delNode + 1,
          (topoServer->nodes[type].count - index - 1) *
              sizeof(struct flagcxTopoNode));
  topoServer->nodes[type].count--;
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoConnectCpus(struct flagcxTopoServer *topoServer) {
  for (int i = 0; i < topoServer->nodes[CPU].count; i++) {
    struct flagcxTopoNode *cpu1 = topoServer->nodes[CPU].nodes + i;
    for (int j = 0; j < topoServer->nodes[CPU].count; j++) {
      struct flagcxTopoNode *cpu2 = topoServer->nodes[CPU].nodes + j;
      if (i == j || (FLAGCX_TOPO_ID_SERVER_ID(cpu1->id) !=
                     FLAGCX_TOPO_ID_SERVER_ID(cpu2->id))) {
        continue;
      }
      float bw;
      FLAGCXCHECK(flagcxTopoGetInterCpuBw(cpu1, &bw));
      FLAGCXCHECK(flagcxTopoConnectNodes(cpu1, cpu2, LINK_SYS, bw));
    }
  }
  return flagcxSuccess;
}

int getBcmGen(uint64_t id, int level) {
  if ((id & 0xfffffffffffff000) == 0x1000c0101000a000)
    return 4;
  if ((id & 0xfffffffffffff000) == (0x1000c03010000000 | level * 0x1000))
    return 5;
  return 0;
}

flagcxResult_t
flagcxTopoFlattenBcmSwitches(struct flagcxTopoServer *topoServer) {
  flagcxResult_t ret = flagcxSuccess;
  for (int s = 0; s < topoServer->nodes[PCI].count; s++) {
    struct flagcxTopoNode *pciSwitch = topoServer->nodes[PCI].nodes + s;
    int gen = getBcmGen(pciSwitch->pci.device, 0);
    // Flatten Gen4 PEX switches in base mode
    if (gen) {
      // Find sub switches with the same device ID.
      int64_t *subSwIds;
      FLAGCXCHECK(flagcxCalloc(&subSwIds, pciSwitch->nlinks));
      int subs = 0;
      for (int l = 0; l < pciSwitch->nlinks; l++) {
        struct flagcxTopoNode *sub = pciSwitch->links[l].remNode;
        // Only fuse sub switches with the same device ID.
        if (sub->type != PCI || getBcmGen(sub->pci.device, 1) != gen)
          continue;
        // Save sub switch for later
        subSwIds[subs++] = sub->id;
        // Remove link to that sub switch
        memmove(pciSwitch->links + l, pciSwitch->links + l + 1,
                (pciSwitch->nlinks - l - 1) * (sizeof(struct flagcxTopoLink)));
        pciSwitch->nlinks--;
        // Don't increase l for the next iteration as we just shifted all links
        // by one.
        l--;
      }

      for (int s = 0; s < subs; s++) {
        // Find sub switch (topoServer->nodes[PCI].nodes is changing every time
        // we remove a node)
        int index;
        FLAGCXCHECKGOTO(
            flagcxTopoIdToIndex(topoServer, PCI, subSwIds[s], &index), ret,
            fail);
        struct flagcxTopoNode *sub = topoServer->nodes[PCI].nodes + index;
        // Connect all sub PCI devices to the parent switch
        for (int l = 0; l < sub->nlinks; l++) {
          struct flagcxTopoNode *remNode = sub->links[l].remNode;
          if (remNode == pciSwitch)
            continue;
          // Add link from parent PCI switch -> PCI device
          if (pciSwitch->nlinks == FLAGCX_TOPO_MAX_LINKS) {
            WARN("Error : too many Topo links (max %d)", FLAGCX_TOPO_MAX_LINKS);
            ret = flagcxInternalError;
            goto fail;
          }
          memcpy(pciSwitch->links + pciSwitch->nlinks, sub->links + l,
                 sizeof(struct flagcxTopoLink));
          pciSwitch->nlinks++;
          // Update link from PCI device -> parent PCI switch
          for (int rl = 0; rl < remNode->nlinks; rl++) {
            if (remNode->links[rl].remNode == sub) {
              remNode->links[rl].remNode = pciSwitch;
              break;
            }
          }
        }
        FLAGCXCHECKGOTO(flagcxTopoRemoveNode(topoServer, PCI, index), ret,
                        fail);
      }
      // Set subdevice to 0xffff to make sure we don't merge this switch again.
      pciSwitch->pci.device |= 0xffff;
      free(subSwIds);
      // Restart, as topoServer->nodes[PCI].nodes has changed.
      s = 0;
      continue;
    fail:
      free(subSwIds);
      return ret;
    }
  }
  return ret;
}

// flagcxResult_t getLocalNetCountByBw(struct flagcxTopoServer* system, int gpu,
// int *count) {
//   int localNetCount = 0, netCountByBw = 0;
//   int* localNets;
//   float totalNetBw = 0, gpuBw = 0;

//   for (int l=0; l<system->nodes[GPU].nodes[gpu].nlinks; l++) {
//     //assuming BW to CPU reflects the GPU bandwidth via P2P or C2C
//     //caveat, this could be wrong if there is a PCIe switch,
//     //and a narrower link to the CPU
//     if (system->nodes[GPU].nodes[gpu].links[l].remNode->type == CPU) {
//        gpuBw = system->nodes[GPU].nodes[gpu].links[l].bw;
//     }
//   }

//   FLAGCXCHECK(flagcxTopoGetLocal(system, GPU, gpu, NET, &localNets,
//   &localNetCount, NULL)); for (int l=0; (l < localNetCount) && (totalNetBw <
//   gpuBw); l++, netCountByBw++) {
//      totalNetBw += system->nodes[GPU].nodes[gpu].paths[NET][localNets[l]].bw;
//   }
//   *count = netCountByBw;

//   free(localNets);
//   return flagcxSuccess;
// }

// a temprarory function to get the local net from topo xml file.
// devId: the device id of the GPU
// netName: the name of the net
// strlen: the length of the netName
static flagcxResult_t flagcxGetLocalNetFromXmlFile(int devId, char *netName,
                                                   int strlen) {
  flagcxResult_t ret = flagcxSuccess;
  flagcxXmlNode *node = NULL;
  int dev = -1;
  // step 1: parse the xml file and load it into flagcxXml struct
  struct flagcxXml *xml;
  const char *xmlTopoFile = flagcxGetEnv("FLAGCX_TOPO_FILE");
  if (!xmlTopoFile) {
    INFO(FLAGCX_ENV, "FLAGCX_TOPO_FILE environment variable not set");
    return ret;
  }
  FLAGCXCHECK(xmlAlloc(&xml, FLAGCX_TOPO_XML_MAX_NODES));
  INFO(FLAGCX_ENV, "FLAGCX_TOPO_FILE set by environment to %s", xmlTopoFile);
  FLAGCXCHECKGOTO(flagcxTopoGetXmlFromFile(xmlTopoFile, xml, 1), ret, fail);

  // step 2: scan flagcxXml struct to find the netName for the given devId
  FLAGCXCHECKGOTO(xmlFindTag(xml, "gpu", &node), ret, fail);
  while (node != NULL) {
    // find the gpu node with the right dev
    FLAGCXCHECKGOTO(xmlGetAttrInt(node, "dev", &dev), ret, fail);
    if (dev == devId) {
      const char *str;
      FLAGCXCHECKGOTO(xmlGetAttr(node, "net", &str), ret, fail);
      if (str != NULL) {
        INFO(FLAGCX_GRAPH, "GPU %d use net %s specified in topo file %s", dev,
             str, xmlTopoFile);
        strncpy(netName, str, strlen - 1);
        netName[strlen - 1] = '\0';
        break;
      } else {
        WARN("GPU %d net attribute is not specified in topo file %s", dev,
             xmlTopoFile);
        ret = flagcxInternalError;
        goto fail;
      }
    }
    flagcxXmlNode *next = NULL;
    FLAGCXCHECKGOTO(xmlFindNextTag(xml, "gpu", node, &next), ret, fail);
    node = next;
  }
  if (dev != devId) {
    // device not found
    WARN("GPU %d not found in topo file %s", devId, xmlTopoFile);
    ret = flagcxInternalError;
    goto fail;
  }
exit:
  free(xml);
  return ret;
fail:
  goto exit;
}

#define FLAGCX_MAX_NET_NAME 128

flagcxResult_t flagcxGetLocalNetFromXml(struct flagcxXml *xml, int apu,
                                        char *name, int strlen) {
  struct flagcxXmlNode *apuNode = NULL;
  FLAGCXCHECK(xmlGetApuByIndex(xml, apu, &apuNode));
  if (apuNode == NULL) {
    WARN("invalid apu index %d", apu);
    return flagcxInternalError;
  }
  struct flagcxXmlNode *netNode = NULL;
  // first try to find the closest net under one CPU node
  FLAGCXCHECK(xmlFindClosestNetUnderCpu(xml, apuNode, &netNode));
  if (netNode == NULL) {
    // if there is no net node that share the same CPU ancestor node with the
    // APU try to find a net node from the server scope
    FLAGCXCHECK(xmlFindClosestNetUnderServer(xml, apuNode, &netNode));
  }
  if (netNode != NULL) {
    // found a net node
    const char *str;
    FLAGCXCHECK(xmlGetAttrStr(netNode, "name", &str)); // get net name
    strncpy(name, str, strlen);
    INFO(FLAGCX_INIT, "local net for apu %d is %s", apu, name);
  }
  return flagcxSuccess;
}

static flagcxResult_t flagcxTopoRankToIndex(struct flagcxTopoServer *topoServer,
                                            int rank, int *index) {
  *index = -1;
  for (int i = 0; i < topoServer->nodes[APU].count; i++) {
    if (topoServer->nodes[APU].nodes[i].apu.rank == rank) {
      *index = i;
      return flagcxSuccess;
    }
  }
  return flagcxInternalError;
}

static flagcxResult_t flagcxTopoGetLocal(struct flagcxTopoServer *topoServer,
                                         int type, int index, int resultType,
                                         int locals[FLAGCX_TOPO_MAX_NODES],
                                         int *localCount, int *pathType) {
  int minType = PATH_DIS;
  float maxBw = 0;
  int count = 0;
  struct flagcxTopoPath *paths =
      topoServer->nodes[type].nodes[index].paths[resultType];
  if (paths == NULL) {
    *localCount = 0;
    return flagcxSuccess;
  }
  for (int i = 0; i < topoServer->nodes[resultType].count; i++) {
    if (paths[i].bw > maxBw ||
        (paths[i].bw == maxBw && paths[i].type < minType)) {
      maxBw = paths[i].bw;
      minType = paths[i].type;
      if (pathType)
        *pathType = minType;
      count = 0;
    }
    if (paths[i].bw == maxBw && paths[i].type == minType) {
      if (count == FLAGCX_TOPO_MAX_NODES) {
        WARN("Error : ran out of room to store found nodes in "
             "flagcxTopoGetLocal."
             " Filled %d of type %d, starting from index %d of type %d.",
             FLAGCX_TOPO_MAX_NODES, resultType, index, type);
        return flagcxInternalError;
      }
      locals[count++] = i;
    }
  }
  *localCount = count;
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoGetLocalNet(struct flagcxTopoServer *topoServer,
                                     int rank, int *netDev) {
  int apu;
  FLAGCXCHECK(flagcxTopoRankToIndex(topoServer, rank, &apu));

  int localNets[FLAGCX_TOPO_MAX_NODES];
  int localNetCount;
  FLAGCXCHECK(flagcxTopoGetLocal(topoServer, APU, apu, NET, localNets,
                                 &localNetCount, NULL));
  if (localNetCount == 0) {
    WARN("Could not find any local path from apu %d to net", apu);
    return flagcxInternalError;
  }

  INFO(FLAGCX_GRAPH, "found %d local nets for apu %d", localNetCount, apu);
  int net = topoServer->nodes[APU].nodes[apu].apu.dev;
  if (isPow2(localNetCount)) { // load balance across apus
    net = mirrorBits(net, localNetCount);
  }
  if (netDev) {
    *netDev =
        topoServer->nodes[NET].nodes[localNets[net % localNetCount]].net.dev;
    INFO(FLAGCX_GRAPH, "local net for apu %d is %d", apu, *netDev);
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxGetLocalNetFromGpu(int apu, int *dev,
                                        struct flagcxHeteroComm *comm) {
  char name[FLAGCX_MAX_NET_NAME + 1] = {0};
  // first try getting local net from existing xml file
  FLAGCXCHECK(flagcxGetLocalNetFromXmlFile(apu, name, FLAGCX_MAX_NET_NAME + 1));
  const char *enable_topo_detect = flagcxGetEnv("FLAGCX_ENABLE_TOPO_DETECT");
  if (strlen(name) == 0) {
    INFO(FLAGCX_GRAPH, "did not find local net for apu %d in xml topo", apu);
    const char *useNet = flagcxGetEnv("FLAGCX_USENET");
    if (useNet != NULL) {
      INFO(FLAGCX_GRAPH,
           "APU %d use net %s specified in FLAGCX_USENET environment variable.",
           apu, useNet);
      strncpy(name, useNet, FLAGCX_MAX_NET_NAME);
    }
  }
  if (strlen(name) != 0) {
    flagcxNetIb.getDevFromName(name, dev);
  }

  if (strlen(name) == 0 && enable_topo_detect &&
      strcmp(enable_topo_detect, "TRUE") == 0) {
    FLAGCXCHECK(flagcxTopoGetLocalNet(comm->topoServer, comm->rank, dev));
  }

  return flagcxSuccess;
}

/****************************/
/* External query functions */
/****************************/

// flagcxResult_t flagcxTopoCpuType(struct flagcxTopoServer* system, int* arch,
// int* vendor, int* model) {
//   *arch = system->nodes[CPU].nodes[0].cpu.arch;
//   *vendor = system->nodes[CPU].nodes[0].cpu.vendor;
//   *model = system->nodes[CPU].nodes[0].cpu.model;
//   return flagcxSuccess;
// }

// FLAGCX_PARAM(IgnoreCpuAffinity, "IGNORE_CPU_AFFINITY", 0);

// flagcxResult_t flagcxTopoGetGpuCount(struct flagcxTopoServer* system, int*
// count) {
//   *count = system->nodes[GPU].count;
//   return flagcxSuccess;
// }

// flagcxResult_t flagcxTopoGetNetCount(struct flagcxTopoServer* system, int*
// count) {
//   *count = system->nodes[NET].count;
//   return flagcxSuccess;
// }

// flagcxResult_t flagcxTopoGetNvsCount(struct flagcxTopoServer* system, int*
// count) {
//   *count = system->nodes[NVS].count;
//   return flagcxSuccess;
// }

// flagcxResult_t flagcxTopoGetCompCap(struct flagcxTopoServer* system, int*
// ccMin, int* ccMax) {
//   if (system->nodes[GPU].count == 0) return flagcxInternalError;
//   int min, max;
//   min = max = system->nodes[GPU].nodes[0].apu.cudaCompCap;
//   for (int g=1; g<system->nodes[GPU].count; g++) {
//     min = std::min(min, system->nodes[GPU].nodes[g].gpu.cudaCompCap);
//     max = std::max(max, system->nodes[GPU].nodes[g].gpu.cudaCompCap);
//   }
//   if (ccMin) *ccMin = min;
//   if (ccMax) *ccMax = max;
//   return flagcxSuccess;
// }

// static flagcxResult_t flagcxTopoPrintRec(struct flagcxTopoNode* node, struct
// flagcxTopoNode* prevNode, char* line, int offset) {
//   if (node->type == GPU) {
//     sprintf(line+offset, "%s/%lx-%lx (%d)", topoNodeTypeStr[node->type],
//     FLAGCX_TOPO_ID_SERVER_ID(node->id), FLAGCX_TOPO_ID_LOCAL_ID(node->id),
//     node->apu.rank);
//   } else if (node->type == CPU) {
//     sprintf(line+offset, "%s/%lx-%lx (%d/%d/%d)",
//     topoNodeTypeStr[node->type], FLAGCX_TOPO_ID_SERVER_ID(node->id),
//     FLAGCX_TOPO_ID_LOCAL_ID(node->id), node->cpu.arch, node->cpu.vendor,
//     node->cpu.model);
//   } else if (node->type == PCI) {
//     sprintf(line+offset, "%s/%lx-%lx (%lx)", topoNodeTypeStr[node->type],
//     FLAGCX_TOPO_ID_SERVER_ID(node->id), FLAGCX_TOPO_ID_LOCAL_ID(node->id),
//     node->pci.device);
//   } else {
//     sprintf(line+offset, "%s/%lx-%lx", topoNodeTypeStr[node->type],
//     FLAGCX_TOPO_ID_SERVER_ID(node->id), FLAGCX_TOPO_ID_LOCAL_ID(node->id));
//   }
//   INFO(FLAGCX_GRAPH, "%s", line);
//   for (int i=0; i<offset; i++) line[i] = ' ';

//   for (int l=0; l<node->nlinks; l++) {
//     struct flagcxTopoLink* link = node->links+l;
//     if (link->type == LINK_LOC) continue;
//     if (link->type != LINK_PCI || link->remNode != prevNode) {
//       sprintf(line+offset, "+ %s[%2.1f] - ", topoLinkTypeStr[link->type],
//       link->bw); int nextOffset = strlen(line); if (link->type == LINK_PCI) {
//         FLAGCXCHECK(flagcxTopoPrintRec(link->remNode, node, line,
//         nextOffset));
//       } else {
//         if (link->remNode->type == NET) {
//           sprintf(line+nextOffset, "%s/%lX (%lx/%d/%f)",
//           topoNodeTypeStr[link->remNode->type], link->remNode->id,
//           link->remNode->net.asic, link->remNode->net.port,
//           link->remNode->net.bw);
//         } else {
//           sprintf(line+nextOffset, "%s/%lX",
//           topoNodeTypeStr[link->remNode->type], link->remNode->id);
//         }
//         INFO(FLAGCX_GRAPH, "%s", line);
//       }
//     }
//   }
//   return flagcxSuccess;
// }

// flagcxResult_t flagcxTopoPrint(struct flagcxTopoServer* s) {
//   INFO(FLAGCX_GRAPH, "=== System : maxBw %2.1f totalBw %2.1f ===", s->maxBw,
//   s->totalBw); char line[1024]; for (int n=0; n<s->nodes[CPU].count; n++)
//   FLAGCXCHECK(flagcxTopoPrintRec(s->nodes[CPU].nodes+n, NULL, line, 0));
//   INFO(FLAGCX_GRAPH, "==========================================");
//   FLAGCXCHECK(flagcxTopoPrintPaths(s));
//   return flagcxSuccess;
// }

// will remove this function when we finish the function that builds server topo
flagcxResult_t flagcxTopoGetXmlTopo(struct flagcxHeteroComm *comm,
                                    struct flagcxXml *xml) {
  // create root node if we didn't get topo from xml file
  if (xml->maxIndex == 0) {
    INFO(FLAGCX_INIT, "creating root XML node");
    // Create top tag
    struct flagcxXmlNode *top;
    // TODO: change root node name from "system" to "root"
    FLAGCXCHECK(xmlAddNode(xml, NULL, "system", &top));
    FLAGCXCHECK(xmlSetAttrInt(top, "version", FLAGCX_TOPO_XML_VERSION));
  }

  INFO(FLAGCX_INIT, "start detecting APUs");
  for (int r = 0; r < comm->nRanks; r++) {
    if (comm->peerInfo[r].hostHash == comm->peerInfo[comm->rank].hostHash) {
      INFO(FLAGCX_INIT, "preparing to detect APU for rank %d", r);
      char busId[FLAGCX_DEVICE_PCI_BUSID_BUFFER_SIZE];
      INFO(FLAGCX_INIT, "converting busId to string");
      FLAGCXCHECK(int64ToBusId(comm->peerInfo[r].busId, busId));
      struct flagcxXmlNode *node;
      FLAGCXCHECK(flagcxTopoFillApu(xml, busId, &node));
      if (node == NULL) {
        continue;
      }
      int devLogicalIdx = 0;
      deviceAdaptor->getDeviceByPciBusId(&devLogicalIdx, busId);
      FLAGCXCHECK(xmlSetAttrInt(node, "dev", devLogicalIdx));
      FLAGCXCHECK(xmlSetAttrInt(node, "rank", r));
    }
  }

  int netDevCount = 0;
  FLAGCXCHECK(flagcxNetIb.devices(&netDevCount));
  for (int n = 0; n < netDevCount; n++) {
    flagcxNetProperties_t props;
    FLAGCXCHECK(flagcxNetIb.getProperties(n, &props));
    struct flagcxXmlNode *netNode;
    FLAGCXCHECK(flagcxTopoFillNet(xml, props.pciPath, props.name, &netNode));
    FLAGCXCHECK(xmlSetAttrInt(netNode, "dev", n));
    FLAGCXCHECK(xmlSetAttrInt(netNode, "speed", props.speed));
    FLAGCXCHECK(xmlSetAttrFloat(netNode, "latency", props.latency));
    FLAGCXCHECK(xmlSetAttrInt(netNode, "port", props.port));
    FLAGCXCHECK(xmlSetAttrLong(netNode, "guid", props.guid));
    FLAGCXCHECK(xmlSetAttrInt(netNode, "maxConn", props.maxComms));
  }

  if (comm->rank == 0) {
    const char *xmlTopoFile = flagcxGetEnv("FLAGCX_TOPO_DUMP_FILE");
    INFO(FLAGCX_ENV, "FLAGCX_TOPO_DUMP_FILE is %s", xmlTopoFile);
    if (xmlTopoFile && comm->rank == 0) {
      INFO(FLAGCX_INIT, "start dumping topo to xml file");
      FLAGCXCHECK(flagcxTopoDumpXmlToFile(xmlTopoFile, xml));
    }
  }
  return flagcxSuccess;
}

struct kvDict kvDictCpuArch[] = {{"x86_64", FLAGCX_TOPO_CPU_ARCH_X86},
                                 {"arm64", FLAGCX_TOPO_CPU_ARCH_ARM},
                                 {"ppc64", FLAGCX_TOPO_CPU_ARCH_POWER},
                                 {NULL, 0}};
struct kvDict kvDictCpuVendor[] = {
    {"GenuineIntel", FLAGCX_TOPO_CPU_VENDOR_INTEL},
    {"AuthenticAMD", FLAGCX_TOPO_CPU_VENDOR_AMD},
    {"CentaurHauls", FLAGCX_TOPO_CPU_VENDOR_ZHAOXIN},
    {"  Shanghai  ", FLAGCX_TOPO_CPU_VENDOR_ZHAOXIN},
    {NULL, 0}};

flagcxResult_t flagcxGetServerId(struct flagcxTopoServer *topoServer,
                                 struct flagcxXmlNode *xmlCpu,
                                 int *serverIdPtr) {
  const char *hostHashStr;
  FLAGCXCHECK(xmlGetAttr(xmlCpu, "host_hash", &hostHashStr));
  uint64_t hostHash = hostHashStr ? strtoull(hostHashStr, NULL, 16) : 0;
  int serverId;
  for (serverId = 0; serverId < topoServer->nHosts; serverId++) {
    if (topoServer->hostHashes[serverId] == hostHash) {
      break;
    }
  }
  // if current host hash hasn't been seen before, this is a new host
  if (serverId == topoServer->nHosts) {
    topoServer->hostHashes[topoServer->nHosts++] = hostHash;
  }
  *serverIdPtr = serverId;
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoAddNet(struct flagcxXmlNode *xmlNet,
                                struct flagcxTopoServer *topoServer,
                                struct flagcxTopoNode *nic, int serverId) {
  int dev;
  FLAGCXCHECK(xmlGetAttrInt(xmlNet, "dev", &dev));

  struct flagcxTopoNode *net;
  FLAGCXCHECK(flagcxTopoCreateNode(topoServer, &net, NET,
                                   FLAGCX_TOPO_ID(serverId, dev)));
  net->net.dev = dev;
  int mbps;
  FLAGCXCHECK(xmlGetAttrLong(xmlNet, "guid", &net->net.guid));
  FLAGCXCHECK(xmlGetAttrIntDefault(xmlNet, "speed", &mbps, 0));
  if (mbps <= 0) {
    mbps = 10000;
  }
  net->net.bw = mbps / 8000.0;
  FLAGCXCHECK(xmlGetAttrFloat(xmlNet, "latency", &net->net.latency));
  FLAGCXCHECK(xmlGetAttrInt(xmlNet, "port", &net->net.port));
  FLAGCXCHECK(xmlGetAttrInt(xmlNet, "maxConn", &net->net.maxConn));

  FLAGCXCHECK(flagcxTopoConnectNodes(nic, net, LINK_NET, net->net.bw));
  FLAGCXCHECK(flagcxTopoConnectNodes(net, nic, LINK_NET, net->net.bw));
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoAddNic(struct flagcxXmlNode *xmlNic,
                                struct flagcxTopoServer *topoServer,
                                struct flagcxTopoNode *nic, int serverId) {
  for (int s = 0; s < xmlNic->nSubs; s++) {
    struct flagcxXmlNode *xmlNet = xmlNic->subs[s];
    if (strcmp(xmlNet->name, "net") != 0)
      continue;
    int index;
    FLAGCXCHECK(xmlGetAttrIndex(xmlNet, "dev", &index));
    if (index == -1)
      continue;
    FLAGCXCHECK(flagcxTopoAddNet(xmlNet, topoServer, nic, serverId));
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoAddApu(struct flagcxXmlNode *xmlApu,
                                struct flagcxTopoServer *topoServer,
                                struct flagcxTopoNode *apu) {
  // we add attributes of the current apu here
  // right now we only have the device logic index of the apu, add more info in
  // the future
  FLAGCXCHECK(xmlGetAttrInt(xmlApu, "dev", &apu->apu.dev));
  FLAGCXCHECK(xmlGetAttrInt(xmlApu, "rank", &apu->apu.rank));
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoAddPci(struct flagcxXmlNode *xmlPci,
                                struct flagcxTopoServer *topoServer,
                                struct flagcxTopoNode *parent, int serverId) {
  const char *str;

  // Assume default type is PCI
  int type = PCI;

  int64_t busId;
  FLAGCXCHECK(xmlGetAttrStr(xmlPci, "busid", &str));
  FLAGCXCHECK(busIdToInt64(str, &busId));

  struct flagcxTopoNode *node = NULL;
  struct flagcxXmlNode *xmlApu = NULL;
  // check if there is any APU attached to current pci device
  FLAGCXCHECK(xmlGetSub(xmlPci, "apu", &xmlApu));
  if (xmlApu != NULL) {
    type = APU;
    // TODO: need to get apu rank info when building xml structure
    // get apu rank here
    FLAGCXCHECK(flagcxTopoCreateNode(topoServer, &node, type,
                                     FLAGCX_TOPO_ID(serverId, busId)));
    FLAGCXCHECK(flagcxTopoAddApu(xmlApu, topoServer, node));
  }
  struct flagcxXmlNode *xmlNic = NULL;
  // check if there is any APU attached to current pci device
  FLAGCXCHECK(xmlGetSub(xmlPci, "nic", &xmlNic));
  if (xmlNic != NULL) {
    type = NIC;
    // Ignore sub device ID and merge multi-port NICs into one PCI device.
    busId &= 0xfffffffffffffff0;
    struct flagcxTopoNode *nicNode = NULL;
    int64_t id = FLAGCX_TOPO_ID(serverId, busId);
    FLAGCXCHECK(flagcxTopoGetNode(topoServer, &nicNode, type, id));
    if (nicNode == NULL) {
      FLAGCXCHECK(flagcxTopoCreateNode(topoServer, &nicNode, type, id));
      node = nicNode;
    }

    FLAGCXCHECK(flagcxTopoAddNic(xmlNic, topoServer, nicNode, serverId));
  } else if (type == PCI) {
    FLAGCXCHECK(flagcxTopoCreateNode(topoServer, &node, type,
                                     FLAGCX_TOPO_ID(serverId, busId)));
    // the following block is essentially storing pci device info into a unint64
    // each of the four attributes is 16bit long
    FLAGCXCHECK(xmlGetAttr(xmlPci, "vendor", &str));
    if (str)
      node->pci.device +=
          strtol(str, NULL, 0)
          << 48; // magic number, see if we can make it a constant
    FLAGCXCHECK(xmlGetAttr(xmlPci, "device", &str));
    if (str)
      node->pci.device += strtol(str, NULL, 0) << 32;
    FLAGCXCHECK(xmlGetAttr(xmlPci, "subsystem_vendor", &str));
    if (str)
      node->pci.device += strtol(str, NULL, 0) << 16;
    FLAGCXCHECK(xmlGetAttr(xmlPci, "subsystem_device", &str));
    if (str)
      node->pci.device += strtol(str, NULL, 0);

    // recursively add sub pci devices
    for (int s = 0; s < xmlPci->nSubs; s++) {
      struct flagcxXmlNode *xmlSubPci = xmlPci->subs[s];
      FLAGCXCHECK(flagcxTopoAddPci(xmlSubPci, topoServer, node, serverId));
    }
  }

  if (node) {
    int width, speed;
    FLAGCXCHECK(xmlGetAttrInt(xmlPci, "link_width", &width));
    FLAGCXCHECK(xmlGetAttrStr(xmlPci, "link_speed", &str));
    if (width == 0)
      width = 16;
    FLAGCXCHECK(kvConvertToInt(str, &speed, kvDictPciGen));
    FLAGCXCHECK(
        flagcxTopoConnectNodes(node, parent, LINK_PCI, width * speed / 80.0));
    FLAGCXCHECK(
        flagcxTopoConnectNodes(parent, node, LINK_PCI, width * speed / 80.0));
  }
  return flagcxSuccess;
}

static flagcxResult_t flagcxTopoGetCpuArch(const char *archStr, int *ret) {
  FLAGCXCHECK(kvConvertToInt(archStr, ret, kvDictCpuArch));
  return flagcxSuccess;
}

static flagcxResult_t flagcxTopoGetCpuVendor(const char *vendorStr, int *ret) {
  FLAGCXCHECK(kvConvertToInt(vendorStr, ret, kvDictCpuVendor));
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoAddCpu(struct flagcxXmlNode *xmlCpu,
                                struct flagcxTopoServer *topoServer) {
  int numaId;
  FLAGCXCHECK(xmlGetAttrInt(xmlCpu, "numaid", &numaId));
  int serverId;
  FLAGCXCHECK(flagcxGetServerId(topoServer, xmlCpu, &serverId));
  struct flagcxTopoNode *cpu;
  FLAGCXCHECK(flagcxTopoCreateNode(topoServer, &cpu, CPU,
                                   FLAGCX_TOPO_ID(serverId, numaId)));
  const char *str;
  FLAGCXCHECK(xmlGetAttr(xmlCpu, "affinity", &str));
  if (str != NULL) {
    FLAGCXCHECK(flagcxStrToCpuset(str, &cpu->cpu.affinity));
  }

  FLAGCXCHECK(xmlGetAttrStr(xmlCpu, "arch", &str));
  FLAGCXCHECK(flagcxTopoGetCpuArch(str, &cpu->cpu.arch));
  if (cpu->cpu.arch == FLAGCX_TOPO_CPU_ARCH_X86) {
    FLAGCXCHECK(xmlGetAttrStr(xmlCpu, "vendor", &str));
    FLAGCXCHECK(flagcxTopoGetCpuVendor(str, &cpu->cpu.vendor));
    if (cpu->cpu.vendor == FLAGCX_TOPO_CPU_VENDOR_INTEL) {
      int familyId, modelId;
      FLAGCXCHECK(xmlGetAttrInt(xmlCpu, "familyid", &familyId));
      FLAGCXCHECK(xmlGetAttrInt(xmlCpu, "modelid", &modelId));
      cpu->cpu.model = (familyId == 6 && modelId >= 0x55)
                           ? FLAGCX_TOPO_CPU_TYPE_SKL
                           : FLAGCX_TOPO_CPU_INTEL_BDW;
    } else if (cpu->cpu.vendor == FLAGCX_TOPO_CPU_VENDOR_ZHAOXIN) {
      int familyId, modelId;
      FLAGCXCHECK(xmlGetAttrInt(xmlCpu, "familyid", &familyId));
      FLAGCXCHECK(xmlGetAttrInt(xmlCpu, "modelid", &modelId));
      if (familyId == 7 && modelId == 0x5B)
        cpu->cpu.model = FLAGCX_TOPO_CPU_TYPE_YONGFENG;
    }
  }
  for (int s = 0; s < xmlCpu->nSubs; s++) {
    struct flagcxXmlNode *node = xmlCpu->subs[s];
    if (strcmp(node->name, "pci") == 0)
      FLAGCXCHECK(flagcxTopoAddPci(node, topoServer, cpu, serverId));
    if (strcmp(node->name, "nic") == 0) {
      struct flagcxTopoNode *nic = NULL;
      FLAGCXCHECK(flagcxTopoGetNode(topoServer, &nic, NIC, 0));
      if (nic == NULL) {
        FLAGCXCHECK(flagcxTopoCreateNode(topoServer, &nic, NIC,
                                         FLAGCX_TOPO_ID(serverId, 0)));
        FLAGCXCHECK(flagcxTopoConnectNodes(cpu, nic, LINK_PCI, LOC_BW));
        FLAGCXCHECK(flagcxTopoConnectNodes(nic, cpu, LINK_PCI, LOC_BW));
      }
      FLAGCXCHECK(flagcxTopoAddNic(node, topoServer, nic, serverId));
    }
  }
  return flagcxSuccess;
}

flagcxResult_t
flagcxTopoGetServerTopoFromXml(struct flagcxXml *xml,
                               struct flagcxTopoServer **topoServer,
                               const uint64_t localHostHash) {
  FLAGCXCHECK(flagcxCalloc(topoServer, 1));
  struct flagcxTopoServer *server = *topoServer;
  // get root node from xml
  struct flagcxXmlNode *topNode;
  FLAGCXCHECK(xmlFindTag(xml, "system", &topNode));
  for (int s = 0; s < topNode->nSubs; s++) {
    struct flagcxXmlNode *node = topNode->subs[s];
    if (strcmp(node->name, "cpu") == 0)
      FLAGCXCHECK(flagcxTopoAddCpu(node, *topoServer));
  }
  // get the correct serverId for current server
  for (int serverId = 0; serverId < server->nHosts; serverId++) {
    if (server->hostHashes[serverId] == localHostHash) {
      server->serverId = serverId;
    }
  }

  // TODO: add CCI links, connect cpu nodes etc.
  FLAGCXCHECK(flagcxTopoFlattenBcmSwitches(*topoServer));
  FLAGCXCHECK(flagcxTopoConnectCpus(*topoServer));

  return flagcxSuccess;
}

static flagcxResult_t flagcxTopoPrintRec(struct flagcxTopoNode *node,
                                         struct flagcxTopoNode *prevNode,
                                         char *line, int offset) {
  if (node->type == APU) {
    // TODO: add rank info
    sprintf(line + offset, "Node [%s/%lx-%lx (%d)]",
            topoNodeTypeStr[node->type], FLAGCX_TOPO_ID_SERVER_ID(node->id),
            FLAGCX_TOPO_ID_LOCAL_ID(node->id), node->apu.rank);
  } else if (node->type == CPU) {
    sprintf(line + offset, "Node [%s/%lx-%lx (%d/%d/%d)]",
            topoNodeTypeStr[node->type], FLAGCX_TOPO_ID_SERVER_ID(node->id),
            FLAGCX_TOPO_ID_LOCAL_ID(node->id), node->cpu.arch, node->cpu.vendor,
            node->cpu.model);
  } else if (node->type == PCI) {
    sprintf(line + offset, "Node [%s/%lx-%lx (%lx)]",
            topoNodeTypeStr[node->type], FLAGCX_TOPO_ID_SERVER_ID(node->id),
            FLAGCX_TOPO_ID_LOCAL_ID(node->id), node->pci.device);
  } else {
    sprintf(line + offset, "Node [%s/%lx-%lx]", topoNodeTypeStr[node->type],
            FLAGCX_TOPO_ID_SERVER_ID(node->id),
            FLAGCX_TOPO_ID_LOCAL_ID(node->id));
  }
  INFO(FLAGCX_GRAPH, "%s", line);
  for (int i = 0; i < offset; i++)
    line[i] = ' ';

  for (int l = 0; l < node->nlinks; l++) {
    struct flagcxTopoLink *link = node->links + l;
    if (link->type == LINK_LOC)
      continue;
    if (link->type != LINK_PCI || link->remNode != prevNode) {
      sprintf(line + offset, "+ Link[%s/%2.1f] - ", topoLinkTypeStr[link->type],
              link->bw);
      int nextOffset = strlen(line);
      if (link->type == LINK_PCI) {
        FLAGCXCHECK(flagcxTopoPrintRec(link->remNode, node, line, nextOffset));
      } else {
        if (link->remNode->type == NET) {
          sprintf(line + nextOffset, "Node [%s/%lx (%lx/%d/%f)]",
                  topoNodeTypeStr[link->remNode->type], link->remNode->id,
                  link->remNode->net.asic, link->remNode->net.port,
                  link->remNode->net.bw);
        } else {
          sprintf(line + nextOffset, "Node [%s/%lx]",
                  topoNodeTypeStr[link->remNode->type], link->remNode->id);
        }
        INFO(FLAGCX_GRAPH, "%s", line);
      }
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoPrint(struct flagcxTopoServer *topoServer) {
  char line[1024];
  // start printing topology from CPU nodes
  INFO(FLAGCX_INIT, "start printing server topology");
  for (int n = 0; n < topoServer->nodes[CPU].count; n++) {
    FLAGCXCHECK(
        flagcxTopoPrintRec(topoServer->nodes[CPU].nodes + n, NULL, line, 0));
  }
  INFO(FLAGCX_GRAPH, "==========================================");
  FLAGCXCHECK(flagcxTopoPrintPaths(topoServer));
  return flagcxSuccess;
}

flagcxResult_t flagcxTopoGetServerTopo(struct flagcxHeteroComm *comm,
                                       struct flagcxTopoServer **topoServer) {
  // TODO: first try to acquire topo from xml file
  struct flagcxXml *xml;
  INFO(FLAGCX_INIT, "allocing flagcxXml");
  FLAGCXCHECK(xmlAlloc(&xml, FLAGCX_TOPO_XML_MAX_NODES));

  FLAGCXCHECK(flagcxTopoGetXmlTopo(comm, xml));
  INFO(FLAGCX_INIT, "start converting xml to serverTopo");
  FLAGCXCHECK(flagcxTopoGetServerTopoFromXml(
      xml, topoServer, comm->peerInfo[comm->rank].hostHash));

  free(xml);
  return flagcxSuccess;
}

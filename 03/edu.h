#pragma once

#include <stdint.h>

struct edu_cfg {
  uint32_t identification;
  uint32_t liveness;
  uint32_t factorial;
  uint32_t _unknown1[5];
  uint32_t factorial_status;
  uint32_t interrupt_status;
  uint32_t _unknown2[14];
  uint32_t interrupt_raise;
  uint32_t interrupt_acknowledge;
  uint32_t _unknown3[6];
  uint64_t dma_src;
  uint64_t dma_dst;
  uint64_t dma_len;
  uint64_t dma_command;
};

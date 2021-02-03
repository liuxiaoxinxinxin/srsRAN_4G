/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2020 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#ifndef SRSLTE_RA_HELPER_H
#define SRSLTE_RA_HELPER_H

#include "srslte/phy/utils/debug.h"
#include <stdint.h>

static inline void ra_helper_compute_s_and_l(uint32_t N, uint32_t v, uint32_t* S, uint32_t* L)
{
  uint32_t low  = v % N;
  uint32_t high = v / N;
  if (high + 1 + low <= N) {
    *S = low;
    *L = high + 1;
  } else {
    *S = N - 1 - low;
    *L = N - high + 1;
  }
}

static int ra_helper_freq_type1(uint32_t N_bwp_size, uint32_t riv, srslte_sch_grant_nr_t* grant)
{
  uint32_t start = 0;
  uint32_t len   = 0;
  ra_helper_compute_s_and_l(N_bwp_size, riv, &start, &len);

  if (start + len > N_bwp_size) {
    ERROR("RIV 0x%x for BWP size %d resulted in freq=%d:%d\n", riv, N_bwp_size, start, len);
    return SRSLTE_ERROR;
  }

  for (uint32_t i = 0; i < start; i++) {
    grant->prb_idx[i] = false;
  }

  for (uint32_t i = start; i < start + len; i++) {
    grant->prb_idx[i] = true;
  }

  for (uint32_t i = start + len; i < SRSLTE_MAX_PRB_NR; i++) {
    grant->prb_idx[i] = false;
  }
  grant->nof_prb = len;

  return SRSLTE_SUCCESS;
}

#endif // SRSLTE_RA_HELPER_H

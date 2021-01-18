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

#ifndef SRSLTE_ZC_SEQUENCE_H
#define SRSLTE_ZC_SEQUENCE_H

#include "srslte/config.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Defines the maximum number of ZC sequence groups (u)
 */
#define SRSLTE_ZC_SEQUENCE_NOF_GROUPS 30

/**
 * @brief Defines the maximum number of base sequences (v)
 */
#define SRSLTE_ZC_SEQUENCE_NOF_BASE 2

/**
 * @brief Generates ZC sequences given the required parameters used in the TS 36 series (LTE)
 *
 * @remark Implemented as defined in TS 36.211 section 5.5.1 Generation of the reference signal sequence
 *
 * @param[in] u Group number {0,1,...29}
 * @param[in] v Base sequence
 * @param[in] alpha Phase shift
 * @param[in] nof_prb Number of PRB
 * @param[out] sequence Output sequence
 * @return SRSLTE_SUCCESS if the generation is successful, SRSLTE_ERROR code otherwise
 */
SRSLTE_API int srslte_zc_sequence_generate_lte(uint32_t u, uint32_t v, float alpha, uint32_t nof_prb, cf_t* sequence);

/**
 * @brief Generates ZC sequences given the required parameters used in the TS 38 series (NR)
 *
 * @remark Implemented as defined in TS 38.211 section 5.2.2 Low-PAPR sequence generation
 *
 * @param u Group number {0,1,...29}
 * @param v base sequence
 * @param alpha Phase shift
 * @param m Number of PRB
 * @param delta Delta parameter described in specification
 * @param sequence Output sequence
 * @return SRSLTE_SUCCESS if the generation is successful, SRSLTE_ERROR code otherwise
 */
SRSLTE_API int
srslte_zc_sequence_generate_nr(uint32_t u, uint32_t v, float alpha, uint32_t m, uint32_t delta, cf_t* sequence);

/**
 * @brief Low-PAPR ZC sequence look-up-table
 */
typedef struct SRSLTE_API {
  uint32_t M_zc;
  uint32_t nof_alphas;
  cf_t*    sequence[SRSLTE_ZC_SEQUENCE_NOF_GROUPS][SRSLTE_ZC_SEQUENCE_NOF_BASE];
} srslte_zc_sequence_lut_t;

/**
 * @brief Initialises a Low-PAPR sequence look-up-table object using NR tables
 *
 * @param q Object pointer
 * @param m Number of PRB
 * @param delta Delta parameter described in specification
 * @param alphas Vector with the alpha shift parameters
 * @param nof_alphas Number alpha shifts to generate
 * @return SRSLTE_SUCCESS if the initialization is successful, SRSLTE_ERROR code otherwise
 */
SRSLTE_API int srslte_zc_sequence_lut_init_nr(srslte_zc_sequence_lut_t* q,
                                              uint32_t                  m,
                                              uint32_t                  delta,
                                              float*                    alphas,
                                              uint32_t                  nof_alphas);

/**
 * @brief Deallocates a Low-PAPR sequence look-up-table object
 * @param q Object pointer
 */
SRSLTE_API void srslte_zc_sequence_lut_free(srslte_zc_sequence_lut_t* q);

/**
 * @brief Get a Low-PAPR sequence from the LUT
 * @param q Object pointer
 * @param u Group number {0,1,...29}
 * @param v base sequence
 * @param alpha_idx Phase shift index
 * @return SRSLTE_SUCCESS if the generation is successful, SRSLTE_ERROR code otherwise
 */
SRSLTE_API const cf_t*
                 srslte_zc_sequence_lut_get(const srslte_zc_sequence_lut_t* q, uint32_t u, uint32_t v, uint32_t alpha_idx);

#endif // SRSLTE_ZC_SEQUENCE_H

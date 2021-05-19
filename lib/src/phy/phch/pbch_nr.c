/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "srsran/phy/phch/pbch_nr.h"
#include "srsran/phy/common/sequence.h"
#include "srsran/phy/fec/polar/polar_chanalloc.h"
#include "srsran/phy/modem/mod.h"
#include "srsran/phy/utils/debug.h"
#include "srsran/phy/utils/vector.h"

/*
 * CRC Parameters
 */
#define PBCH_NR_CRC SRSRAN_LTE_CRC24C
#define PBCH_NR_CRC_LEN 24

/*
 * Polar code N_max
 */
#define PBCH_NR_POLAR_N_MAX 9U

/*
 * Polar rate matching I_BIL
 */
#define pbch_nr_polar_rm_tx_IBIL 0

/*
 * Number of generated payload bits, called A
 */
#define PBCH_NR_A (SRSRAN_PBCH_NR_PAYLOAD_SZ + 8)

/*
 * Number of payload bits plus CRC
 */
#define PBCH_NR_K (PBCH_NR_A + PBCH_NR_CRC_LEN)

/*
 * Number of Polar encoded bits
 */
#define PBCH_NR_N (1U << PBCH_NR_POLAR_N_MAX)

/*
 * Number of RM bits
 */
#define PBCH_NR_E (864)

/*
 * Number of symbols
 */
#define PBCH_NR_M (PBCH_NR_E / 2)

/*
 * Number of DMRS
 */
#define PBCH_NR_NOF_DMRS (143)

static int pbch_nr_init_encoder(srsran_pbch_nr_t* q, const srsran_pbch_nr_args_t* args)
{
  // Skip encoder init if not requested
  if (!args->enable_encode) {
    return SRSRAN_SUCCESS;
  }

  srsran_polar_encoder_type_t encoder_type = SRSRAN_POLAR_ENCODER_PIPELINED;

#ifdef LV_HAVE_AVX2
  if (!args->disable_simd) {
    encoder_type = SRSRAN_POLAR_ENCODER_AVX2;
  }
#endif /* LV_HAVE_AVX2 */

  if (srsran_polar_encoder_init(&q->polar_encoder, encoder_type, PBCH_NR_POLAR_N_MAX) < SRSRAN_SUCCESS) {
    ERROR("Error initiating polar encoder");
    return SRSRAN_ERROR;
  }

  if (srsran_polar_rm_tx_init(&q->polar_rm_tx) < SRSRAN_SUCCESS) {
    ERROR("Error initiating polar RM");
    return SRSRAN_ERROR;
  }

  if (srsran_modem_table_lte(&q->qpsk, SRSRAN_MOD_QPSK) < SRSRAN_SUCCESS) {
    return SRSRAN_ERROR;
  }

  return SRSRAN_SUCCESS;
}

static int pbch_nr_init_decoder(srsran_pbch_nr_t* q, const srsran_pbch_nr_args_t* args)
{
  // Skip decoder init if not requested
  if (!args->enable_decode) {
    return SRSRAN_SUCCESS;
  }

  srsran_polar_decoder_type_t decoder_type = SRSRAN_POLAR_DECODER_SSC_C;

#ifdef LV_HAVE_AVX2
  if (!args->disable_simd) {
    decoder_type = SRSRAN_POLAR_DECODER_SSC_C_AVX2;
  }
#endif /* LV_HAVE_AVX2 */

  if (srsran_polar_decoder_init(&q->polar_decoder, decoder_type, PBCH_NR_POLAR_N_MAX) < SRSRAN_SUCCESS) {
    ERROR("Error initiating polar decoder");
    return SRSRAN_ERROR;
  }

  if (srsran_polar_rm_rx_init_c(&q->polar_rm_rx) < SRSRAN_SUCCESS) {
    ERROR("Error initiating polar RM");
    return SRSRAN_ERROR;
  }

  return SRSRAN_SUCCESS;
}

int srsran_pbch_nr_init(srsran_pbch_nr_t* q, const srsran_pbch_nr_args_t* args)
{
  if (q == NULL || args == NULL) {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }

  if (!args->enable_encode && !args->enable_decode) {
    ERROR("Encoder and decoder are disabled, at least one of them shall be active");
    return SRSRAN_ERROR;
  }

  if (pbch_nr_init_encoder(q, args) < SRSRAN_SUCCESS) {
    return SRSRAN_ERROR;
  }

  if (pbch_nr_init_decoder(q, args) < SRSRAN_SUCCESS) {
    return SRSRAN_ERROR;
  }

  if (srsran_crc_init(&q->crc, PBCH_NR_CRC, PBCH_NR_CRC_LEN) < SRSRAN_SUCCESS) {
    return SRSRAN_ERROR;
  }

  if (srsran_polar_code_init(&q->code) < SRSRAN_SUCCESS) {
    return SRSRAN_ERROR;
  }

  if (srsran_polar_code_get(&q->code, PBCH_NR_K, PBCH_NR_E, PBCH_NR_POLAR_N_MAX) < SRSRAN_SUCCESS) {
    ERROR("Error Getting polar code");
    return SRSRAN_ERROR;
  }

  return SRSRAN_SUCCESS;
}

void srsran_pbch_nr_free(srsran_pbch_nr_t* q)
{
  if (q == NULL) {
    return;
  }
  srsran_polar_encoder_free(&q->polar_encoder);
  srsran_polar_decoder_free(&q->polar_decoder);
  srsran_polar_rm_rx_free_c(&q->polar_rm_rx);
  srsran_polar_rm_tx_free(&q->polar_rm_tx);
  srsran_polar_code_free(&q->code);
  srsran_modem_table_free(&q->qpsk);
  SRSRAN_MEM_ZERO(q, srsran_pbch_nr_t, 1);
}

/*
 *  Implements TS 38.212 Table 7.1.1-1: Value of PBCH payload interleaver pattern G ( j )
 */
static const uint32_t G[PBCH_NR_A] = {16, 23, 18, 17, 8,  30, 10, 6,  24, 7,  0,  5,  3,  2,  1,  4,
                                      9,  11, 12, 13, 14, 15, 19, 20, 21, 22, 25, 26, 27, 28, 29, 31};

static void
pbch_nr_pbch_msg_pack(const srsran_pbch_nr_cfg_t* cfg, const srsran_pbch_msg_nr_t* msg, uint8_t a[PBCH_NR_A])
{
  // Extract actual payload size
  uint32_t A_hat = SRSRAN_PBCH_NR_PAYLOAD_SZ;

  // Put SFN in a_hat[A_hat] to a_hat[A_hat + 3]
  uint32_t j_sfn = 0;
  a[G[j_sfn++]]  = (uint8_t)((msg->sfn_4lsb >> 3U) & 1U); // 4th LSB of SFN
  a[G[j_sfn++]]  = (uint8_t)((msg->sfn_4lsb >> 2U) & 1U); // 3th LSB of SFN
  a[G[j_sfn++]]  = (uint8_t)((msg->sfn_4lsb >> 1U) & 1U); // 2th LSB of SFN
  a[G[j_sfn++]]  = (uint8_t)((msg->sfn_4lsb >> 0U) & 1U); // 1th LSB of SFN

  // Put HRF in a_hat[A_hat + 4]
  a[G[10]] = (msg->hrf ? 1 : 0);

  // Put SSB related in a_hat[A_hat + 5] to a_hat[A_hat + 7]
  if (cfg->Lmax == 64) {
    a[G[11]] = (uint8_t)((msg->ssb_idx >> 5U) & 1U); // 6th bit of SSB index
    a[G[12]] = (uint8_t)((msg->ssb_idx >> 4U) & 1U); // 5th bit of SSB index
    a[G[13]] = (uint8_t)((msg->ssb_idx >> 3U) & 1U); // 4th bit of SSB index
  } else {
    a[G[11]] = (uint8_t)msg->k_ssb_msb; // 6th bit of SSB index
    a[G[12]] = 0;                       // Reserved
    a[G[13]] = 0;                       // Reserved
  }

  // Put actual payload
  uint32_t j_other = 14;
  for (uint32_t i = 0; i < A_hat; i++) {
    if (i > 0 && i < 7) {
      a[G[j_sfn++]] = msg->payload[i];
    } else {
      a[G[j_other++]] = msg->payload[i];
    }
  }
}

static void pbch_nr_scramble(const srsran_pbch_nr_cfg_t* cfg, const uint8_t a[PBCH_NR_A], uint8_t a_prime[PBCH_NR_A])
{
  uint32_t i = 0;
  uint32_t j = 0;

  // Initialise sequence
  srsran_sequence_state_t sequence_state = {};
  srsran_sequence_state_init(&sequence_state, SRSRAN_SEQUENCE_MOD(cfg->N_id));

  // Select value M
  uint32_t M = PBCH_NR_A - 3;
  if (cfg->Lmax == 64) {
    M = PBCH_NR_A - 6;
  }

  // Select value v
  uint32_t v = 2 * a[G[1]] + a[G[2]];

  // Advance sequence
  srsran_sequence_state_advance(&sequence_state, M * v);

  // Generate actual sequence
  uint8_t c[PBCH_NR_A] = {};
  srsran_sequence_state_apply_bit(&sequence_state, c, c, PBCH_NR_A);

  while (i < PBCH_NR_A) {
    // a i corresponds to any one of the bits belonging to the SS/PBCH block index, the half frame index, and 2 nd and 3
    // rd least significant bits of the system frame number
    uint8_t s_i = c[j];

    // else
    if (i == G[11] || i == G[12] || i == G[13] || i == G[10] || i == G[1] || i == G[2]) {
      s_i = 0;
    } else {
      j++;
    }

    a_prime[i] = a[i] ^ s_i;
    i++;
  }
}

static int pbch_nr_polar_encode(srsran_pbch_nr_t* q, const uint8_t c[PBCH_NR_K], uint8_t d[PBCH_NR_K])
{
  // Allocate channel
  uint8_t allocated[PBCH_NR_N];
  srsran_polar_chanalloc_tx(c, allocated, q->code.N, q->code.K, q->code.nPC, q->code.K_set, q->code.PC_set);

  // Encode bits
  if (srsran_polar_encoder_encode(&q->polar_encoder, allocated, d, q->code.n) < SRSRAN_SUCCESS) {
    return SRSRAN_ERROR;
  }

  return SRSRAN_SUCCESS;
}

static int pbch_nr_polar_rm_tx(srsran_pbch_nr_t* q, const uint8_t d[PBCH_NR_K], uint8_t o[PBCH_NR_E])
{
  if (srsran_polar_rm_tx(&q->polar_rm_tx, d, o, q->code.n, PBCH_NR_E, PBCH_NR_K, pbch_nr_polar_rm_tx_IBIL) <
      SRSRAN_SUCCESS) {
    return SRSRAN_ERROR;
  }

  return SRSRAN_SUCCESS;
}

static void pbch_nr_scramble_tx(const srsran_pbch_nr_cfg_t* cfg,
                                uint32_t                    ssb_idx,
                                const uint8_t               b[PBCH_NR_E],
                                uint8_t                     b_hat[PBCH_NR_E])
{
  // Initialise sequence
  srsran_sequence_state_t sequence_state = {};
  srsran_sequence_state_init(&sequence_state, SRSRAN_SEQUENCE_MOD(cfg->N_id));

  // Select value M
  uint32_t M_bit = PBCH_NR_E;

  // Select value v
  uint32_t v = (ssb_idx & 0x7U);
  if (cfg->Lmax == 4) {
    v = ssb_idx & 0x3U;
  }

  // Advance sequence
  srsran_sequence_state_advance(&sequence_state, v * M_bit);

  // Apply sequence
  srsran_sequence_state_apply_bit(&sequence_state, b, b_hat, PBCH_NR_E);
}

static void
pbch_nr_mapping(const srsran_pbch_nr_cfg_t* cfg, const cf_t symbols[PBCH_NR_E], cf_t ssb_grid[SRSRAN_SSB_NOF_RE])
{
  uint32_t count = 0;

  // PBCH DMRS shift
  uint32_t v = cfg->N_id % 4;

  // Symbol 1
  for (uint32_t k = 0; k < SRSRAN_SSB_BW_SUBC; k++) {
    // Skip DMRS
    if (k % 4 == v) {
      continue;
    }

    ssb_grid[1 * SRSRAN_SSB_BW_SUBC + k] = symbols[count++];
  }

  // Symbol 2
  for (uint32_t k = 0; k < 48; k++) {
    // Skip DMRS
    if (k % 4 == v) {
      continue;
    }

    ssb_grid[2 * SRSRAN_SSB_BW_SUBC + k] = symbols[count++];
  }
  for (uint32_t k = 192; k < SRSRAN_SSB_BW_SUBC; k++) {
    // Skip DMRS
    if (k % 4 == v) {
      continue;
    }

    ssb_grid[2 * SRSRAN_SSB_BW_SUBC + k] = symbols[count++];
  }

  // Symbol 3
  for (uint32_t k = 0; k < SRSRAN_SSB_BW_SUBC; k++) {
    // Skip DMRS
    if (k % 4 == v) {
      continue;
    }

    ssb_grid[3 * SRSRAN_SSB_BW_SUBC + k] = symbols[count++];
  }
}

int srsran_pbch_nr_encode(srsran_pbch_nr_t*           q,
                          const srsran_pbch_nr_cfg_t* cfg,
                          const srsran_pbch_msg_nr_t* msg,
                          cf_t                        ssb_grid[SRSRAN_SSB_NOF_RE])
{
  if (q == NULL || cfg == NULL || msg == NULL || ssb_grid == NULL) {
    return SRSRAN_ERROR_INVALID_INPUTS;
  }

  // TS 38.212 7.1 Broadcast channel
  // 7.1.1  PBCH payload generation
  uint8_t a[PBCH_NR_A];
  pbch_nr_pbch_msg_pack(cfg, msg, a);

  // 7.1.2 Scrambling
  uint8_t b[PBCH_NR_K];
  pbch_nr_scramble(cfg, a, b);

  // 7.1.3 Transport block CRC attachment
  uint32_t checksum = srsran_crc_attach(&q->crc, b, PBCH_NR_A);
  INFO("NR-PBCH: checksum=%06x", checksum);

  // 7.1.4 Channel coding
  uint8_t d[PBCH_NR_K];
  if (pbch_nr_polar_encode(q, b, d) < SRSRAN_SUCCESS) {
    return SRSRAN_ERROR;
  }

  // 7.1.5 Rate matching
  uint8_t f[PBCH_NR_E];
  if (pbch_nr_polar_rm_tx(q, d, f) < SRSRAN_SUCCESS) {
    return SRSRAN_ERROR;
  }

  // TS 38.211 7.3.3 Physical broadcast channel
  // 7.3.3.1 Scrambling
  pbch_nr_scramble_tx(cfg, msg->ssb_idx, f, f);

  // 7.3.3.2 Modulation
  cf_t symbols[PBCH_NR_M];
  srsran_mod_modulate(&q->qpsk, f, symbols, PBCH_NR_E);

  // 7.3.3.3 Mapping to physical resources
  // 7.4.3.1.3 Mapping of PBCH and DM-RS within an SS/PBCH block
  pbch_nr_mapping(cfg, symbols, ssb_grid);

  return SRSRAN_SUCCESS;
}

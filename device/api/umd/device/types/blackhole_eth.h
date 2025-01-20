/*
 * SPDX-FileCopyrightText: (c) 2025 Tenstorrent Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

namespace tt::umd {

namespace blackhole {

static constexpr uint32_t NUM_SERDES_LANES = 8;

typedef enum {
    LOOPBACK_NONE,
    LOOPBACK_NEAR_END_MAC,
    LOOPBACK_NEAR_END_FIFO,
    LOOPBACK_NEAR_END_PMA,
    LOOPBACK_FAR_END_FIFO,
    LOOPBACK_SERDES_NEP,
    LOOPBACK_SERDES_NES_PREDRIVER,
} loopback_mode_e;

typedef enum {
    AW_MANUAL_EQ,
    AW_ANLT_MODE,
    AW_LT_MODE,
} link_train_mode_e;

typedef enum {
    ORION,
    P100,
    P150,
    P300,
    UBB,
} pcb_type_e;

typedef enum {
    LINK_TRAIN_TRAINING,
    LINK_TRAIN_SKIP,
    LINK_TRAIN_PASS,
    LINK_TRAIN_INT_LB,
    LINK_TRAIN_EXT_LB,
    LINK_TRAIN_TIMEOUT_MANUAL_EQ,
    LINK_TRAIN_TIMEOUT_ANLT,
    LINK_TRAIN_TIMEOUT_CDR_LOCK,
    LINK_TRAIN_TIMEOUT_BIST_LOCK,
    LINK_TRAIN_TIMEOUT_LINK_UP,
    LINK_TRAIN_TIMEOUT_CHIP_INFO,
} link_train_status_e;

typedef enum {
    PORT_UNKNOWN,
    PORT_UP,
    PORT_DOWN,
    PORT_UNUSED,
} port_status_e;

typedef enum {
    NRZ_1p25,
    NRZ_10p3125,
    NRZ_25p78125,
    NRZ_26p5625,
    NRZ_53p125,
    PAM4_53p125,
    PAM4_106p25,
    NUM_RATES,
} serdes_rate_e;

typedef enum {
    AW_128W = 7,
    AW_64W = 6,
    AW_40W = 5,
    AW_32W = 4,
    AW_20W = 3,
    AW_16W = 2,
    AW_10W = 1,
} serdes_width_e;

typedef enum {
    AW_TIMER,
    AW_DWELL,
} serdes_bist_mode_e;

typedef enum {
    AW_PRBS7,
    AW_PRBS9,
    AW_PRBS11,
    AW_PRBS13,
    AW_PRBS15,
    AW_PRBS23,
    AW_PRBS31,
    AW_QPRBS13,
    AW_JP03A,
    AW_JP03B,
    AW_LINEARITY_PATTERN,
    AW_USER_DEFINED_PATTERN,
    AW_FULL_RATE_CLOCK,
    AW_HALF_RATE_CLOCK,
    AW_QUARTER_RATE_CLOCK,
    AW_PATT_32_1S_32_0S,
    AW_BIST_PATTERN_MAX,
} serdes_bist_pattern_e;

typedef enum {
    AW_FULL_EQ,
    AW_EVAL_ONLY,
    AW_INIT_EVAL,
    AW_CLEAR_EVAL,
    AW_FULL_EQ_FOM,
    AW_EVAL_ONLY_FOM,
} serdes_eq_type_e;

typedef struct {
    uint32_t patch : 8;
    uint32_t minor : 8;
    uint32_t major : 8;
    uint32_t unused : 8;
} fw_version_t;

typedef struct {
    uint8_t pcb_type;  // 0
    uint8_t asic_location;
    uint8_t eth_id;
    uint8_t logical_eth_id;
    uint32_t board_id_hi;   // 1
    uint32_t board_id_lo;   // 2
    uint32_t mac_addr_org;  // 3
    uint32_t mac_addr_id;   // 4
    uint32_t spare[2];      // 5-6
    uint32_t ack;           // 7
} chip_info_t;

typedef struct {
    uint32_t bist_mode;  // 0
    uint32_t test_time;  // 1
    // test_time in cycles for bist mode 0 and ms for bist mode 1
    uint32_t error_cnt_nt[NUM_SERDES_LANES];           // 2-9
    uint32_t error_cnt_55t32_nt[NUM_SERDES_LANES];     // 10-17
    uint32_t error_cnt_overflow_nt[NUM_SERDES_LANES];  // 18-25
} serdes_rx_bist_results_t;

typedef struct {
    // Basic status
    uint32_t postcode;                 // 0
    port_status_e port_status;         // 1
    link_train_status_e train_status;  // 2
    uint32_t train_speed;              // 3 - Actual resulting speed from training

    // Live status/retrain related
    uint32_t retrain_count;   // 4
    uint32_t mac_pcs_errors;  // 5
    uint32_t corr_dw_hi;      // 6
    uint32_t corr_dw_lo;      // 7
    uint32_t uncorr_dw_hi;    // 8
    uint32_t uncorr_dw_lo;    // 9
    uint32_t frames_rxd_hi;   // 10
    uint32_t frames_rxd_lo;   // 11
    uint32_t bytes_rxd_hi;    // 12
    uint32_t bytes_rxd_lo;    // 13

    uint32_t spare[28 - 14];  // 14-27

    // Heartbeat
    uint32_t heartbeat[4];  // 28-31
} eth_status_t;

typedef struct {
    uint32_t postcode;           // 0
    uint32_t serdes_inst;        // 1
    uint32_t serdes_lane_mask;   // 2
    uint32_t target_speed;       // 3 - Target speed from the boot params
    uint32_t data_rate;          // 4
    uint32_t data_width;         // 5
    uint32_t spare_main[8 - 6];  // 6-7

    // Training retries
    uint32_t lt_retry_cnt;   // 8
    uint32_t spare[16 - 9];  // 9-15

    // BIST
    uint32_t bist_mode;       // 16
    uint32_t bist_test_time;  // 17
    // test_time in cycles for bist mode 0 and ms for bist mode 1
    uint32_t bist_err_cnt_nt[NUM_SERDES_LANES];           // 18-25
    uint32_t bist_err_cnt_55t32_nt[NUM_SERDES_LANES];     // 26-33
    uint32_t bist_err_cnt_overflow_nt[NUM_SERDES_LANES];  // 34-41

    uint32_t spare2[48 - 42];  // 42-47

    // Training times
    uint32_t man_eq_cmn_pstate_time;      // 48
    uint32_t man_eq_tx_ack_time;          // 49
    uint32_t man_eq_rx_ack_time;          // 50
    uint32_t man_eq_rx_iffsm_time;        // 51
    uint32_t man_eq_rx_eq_assert_time;    // 52
    uint32_t man_eq_rx_eq_deassert_time;  // 53
    uint32_t anlt_auto_neg_time;          // 54
    uint32_t anlt_link_train_time;        // 55
    uint32_t cdr_lock_time;               // 56
    uint32_t bist_lock_time;              // 57

    uint32_t spare_time[64 - 58];  // 58-63
} serdes_results_t;

typedef struct {
    uint32_t postcode;  // 0

    uint32_t spare[24 - 1];  // 1-23

    // Training times
    uint32_t link_up_time;    // 24
    uint32_t chip_info_time;  // 25

    uint32_t spare_time[32 - 26];  // 26-31
} macpcs_results_t;

typedef struct {
    eth_status_t eth_status;          // 0-31
    serdes_results_t serdes_results;  // 32 - 95
    macpcs_results_t macpcs_results;  // 96 - 127

    uint32_t spare[238 - 128];  // 128 - 237

    fw_version_t serdes_fw_ver;  // 238
    fw_version_t eth_fw_ver;     // 239
    chip_info_t local_info;      // 240 - 247
    chip_info_t remote_info;     // 248 - 255
} boot_results_t;

constexpr uint32_t BOOT_RESULTS_ADDR = 0x7CC00;

}  // namespace blackhole

}  // namespace tt::umd

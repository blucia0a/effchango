#pragma once

// Register bits/values
#define         HM0360_MODEL_ID                 0x0360

#define         HIMAX_RESET                     0x01
#define         HIMAX_MODE_STANDBY              0x00
#define         HIMAX_MODE_STREAMING            0x01     // I2C triggered streaming enable
#define         HIMAX_MODE_STREAMING_NFRAMES    0x03     // Output N frames
#define         HIMAX_MODE_STREAMING_TRIG       0x05     // Hardware Trigger

#define         HIMAX_LINE_LEN_PCK_QQVGA        0x300
#define         HIMAX_FRAME_LENGTH_QQVGA        0x084

#define         HIMAX_LINE_LEN_PCK_QVGA         0x300
#define         HIMAX_FRAME_LENGTH_QVGA         0x109

#define         HIMAX_LINE_LEN_PCK_VGA          0x300
#define         HIMAX_FRAME_LENGTH_VGA          0x214

// Register set
// Read only
#define         MODEL_ID_H                      0x0000
#define         MODEL_ID_L                      0x0001
#define         SILICON_REV                     0x0002
#define         FRAME_COUNT_H                   0x0005
#define         FRAME_COUNT_L                   0x0006
#define         PIXEL_ORDER                     0x0007
// Sensor mode control
#define         MODE_SELECT                     0x0100
#define         IMG_ORIENTATION                 0x0101
#define         EMBEDDED_LINE_EN                0x0102
#define         SW_RESET                        0x0103
#define         COMMAND_UPDATE                  0x0104
// Sensor exposure gain control
#define         INTEGRATION_H                   0x0202
#define         INTEGRATION_L                   0x0203
#define         ANALOG_GAIN                     0x0205
#define         DIGITAL_GAIN_H                  0x020E
#define         DIGITAL_GAIN_L                  0x020F
// Clock control
#define         PLL1_CONFIG                     0x0300
#define         PLL2_CONFIG                     0x0301
#define         PLL3_CONFIG                     0x0302
// Frame timing control
#define         FRAME_LEN_LINES_H               0x0340
#define         FRAME_LEN_LINES_L               0x0341
#define         LINE_LEN_PCK_H                  0x0342
#define         LINE_LEN_PCK_L                  0x0343
// Monochrome programming
#define         MONO_MODE                       0x0370
#define         MONO_MODE_ISP                   0x0371
#define         MONO_MODE_SEL                   0x0372
// Binning mode control
#define         H_SUBSAMPLE                     0x0380
#define         V_SUBSAMPLE                     0x0381
#define         BINNING_MODE                    0x0382
// Test pattern control
#define         TEST_PATTERN_MODE               0x0601
// Black level control
#define         BLC_TGT                         0x1004
#define         BLC2_TGT                        0x1009
#define         MONO_CTRL                       0x100A
// VSYNC / HSYNC / pixel shift
#define         OPFM_CTRL                       0x1014
// Tone mapping registers
#define         CMPRS_CTRL                      0x102F
#define         CMPRS_01                        0x1030
#define         CMPRS_02                        0x1031
#define         CMPRS_03                        0x1032
#define         CMPRS_04                        0x1033
#define         CMPRS_05                        0x1034
#define         CMPRS_06                        0x1035
#define         CMPRS_07                        0x1036
#define         CMPRS_08                        0x1037
#define         CMPRS_09                        0x1038
#define         CMPRS_10                        0x1039
#define         CMPRS_11                        0x103A
#define         CMPRS_12                        0x103B
#define         CMPRS_13                        0x103C
#define         CMPRS_14                        0x103D
#define         CMPRS_15                        0x103E
#define         CMPRS_16                        0x103F
// Automatic exposure control
#define         AE_CTRL                         0x2000
#define         AE_CTRL1                        0x2001
#define         CNT_ORGH_H                      0x2002
#define         CNT_ORGH_L                      0x2003
#define         CNT_ORGV_H                      0x2004
#define         CNT_ORGV_L                      0x2005
#define         CNT_STH_H                       0x2006
#define         CNT_STH_L                       0x2007
#define         CNT_STV_H                       0x2008
#define         CNT_STV_L                       0x2009
#define         CTRL_PG_SKIPCNT                 0x200A
#define         BV_WIN_WEIGHT_EN                0x200D
#define         MAX_INTG_H                      0x2029
#define         MAX_INTG_L                      0x202A
#define         MAX_AGAIN                       0x202B
#define         MAX_DGAIN_H                     0x202C
#define         MAX_DGAIN_L                     0x202D
#define         MIN_INTG                        0x202E
#define         MIN_AGAIN                       0x202F
#define         MIN_DGAIN                       0x2030
#define         T_DAMPING                       0x2031
#define         N_DAMPING                       0x2032
#define         ALC_TH                          0x2033
#define         AE_TARGET_MEAN                  0x2034
#define         AE_MIN_MEAN                     0x2035
#define         AE_TARGET_ZONE                  0x2036
#define         CONVERGE_IN_TH                  0x2037
#define         CONVERGE_OUT_TH                 0x2038
#define         FS_CTRL                         0x203B
#define         FS_60HZ_H                       0x203C
#define         FS_60HZ_L                       0x203D
#define         FS_50HZ_H                       0x203E
#define         FS_50HZ_L                       0x203F
#define         FRAME_CNT_TH                    0x205B
#define         AE_MEAN                         0x205D
#define         AE_CONVERGE                     0x2060
#define         AE_BLI_TGT                      0x2070
// Interrupt control
#define         PULSE_MODE                      0x2061
#define         PULSE_TH_H                      0x2062
#define         PULSE_TH_L                      0x2063
#define         INT_INDIC                       0x2064
#define         INT_CLEAR                       0x2065
// Motion detection control
#define         MD_CTRL                         0x2080
#define         ROI_START_END_V                 0x2081
#define         ROI_START_END_H                 0x2082
#define         MD_TH_MIN                       0x2083
#define         MD_TH_STR_L                     0x2084
#define         MD_TH_STR_H                     0x2085
#define         MD_LIGHT_COEF                   0x2099
#define         MD_BLOCK_NUM_TH                 0x209B
#define         MD_LATENCY                      0x209C
#define         MD_LATENCY_TH                   0x209D
#define         MD_CTRL1                        0x209E
// Sync function control
#define         EXP_SYNC_CFG                    0x3010
#define         ERR_FLAG_CFG                    0x3013
#define         OFFSET_RDSYNC_H                 0x3019
#define         OFFSET_RDSYNC_L                 0x301A
#define         RDSYNC_DEC_TH_H                 0x301B
#define         RDSYNC_DEC_TH_L                 0x301C
// Context switch control
#define         PMU_CFG_3                       0x3024
#define         PMU_CFG_4                       0x3025
// Operation mode
#define         PMU_CFG_5                       0x3026
#define         PMU_CFG_6                       0x3027
#define         PMU_CFG_7                       0x3028
#define         PMU_CFG_8                       0x3029
#define         PMU_CFG_9                       0x302A
// ROI and sensor control
#define         WIN_MODE                        0x3030
#define         ROI_CFG                         0x3060
// Strobe control
#define         STROBE_CFG                      0x3080
// IO and clock control
#define         VSYNC_FRONT                     0x3094
#define         VSYNC_END                       0x3095
#define         HSYNC_FRONT_H                   0x3096
#define         HSYNC_FRONT_L                   0x3097
#define         HSYNC_END_H                     0x3098
#define         HSYNC_END_L                     0x3099
#define         PCLKO_GATED_EN                  0x309E
#define         PCLKO_FRAME_FRONT               0x309F
#define         PCLKO_FRAME_END                 0x30A0
#define         PCLKO_LINE_FRONT_H              0x30A1
#define         PCLKO_LINE_FRONT_L              0x30A2
#define         PCLKO_LINE_END_H                0x30A3
#define         PCLKO_LINE_END_L                0x30A4
#define         OUTPUT_EN                       0x30A5
#define         FRAME_OUTPUT_EN                 0x30A8
#define         MULTI_CAMERA_CONFIG             0x30A9
#define         MULTI_CAMERA_TUNE_H             0x30AA
#define         MULTI_CAMERA_TUNE_L             0x30AB
#define         ANA_REGISTER_04                 0x310F
#define         ANA_REGISTER_07                 0x3112
#define         PLL_POST_DIV_D                  0x3128
// Context switch A
#define         CTX_A_AE_CTRL                   0x3512
// Context switch B
#define         CTX_B_AE_CTRL                   0x356C

typedef struct {
    eff_pinmux_t    *enable_pinmux;
    eff_gpio_t      *enable_gpio;
    uint32_t        enable_pin;
    eff_pinmux_t    *pwdn_pinmux;
    eff_gpio_t      *pwdn_gpio;
    uint32_t        pwdn_pin;
    eff_pinmux_t    *vsync_pinmux;
    eff_gpio_t      *vsync_gpio;
    uint32_t        vsync_pin;

    eff_i2c_t       *i2c;
    eff_spi_t       *spi;
} hm0360_interface_t;

typedef enum {
    HM0360_RESOLUTION_QQVGA = 0,    // 160 x 120
    HM0360_RESOLUTION_QVGA = 1,     // 320 x 240
    HM0360_RESOLUTION_VGA = 2       // 640 x 480
} hm0360_resolution_e;

// User functions
int8_t hm0360_interface_init(hm0360_interface_t *interface);

int8_t hm0360_power_on(hm0360_interface_t *interface);
int8_t hm0360_power_off(hm0360_interface_t *interface);
int8_t hm0360_sw_reset(hm0360_interface_t *interface);
int8_t hm0360_hw_reset(hm0360_interface_t *interface);

int8_t hm0360_get_model_id(hm0360_interface_t *interface, uint16_t *id);
int8_t hm0360_set_resolution(hm0360_interface_t *interface, hm0360_resolution_e resolution);

// Raw register access
int8_t hm0360_write_reg(uint16_t reg_addr, uint8_t reg_val);
int8_t hm0360_read_reg(uint16_t reg_addr, uint8_t *reg_val);

extern const uint16_t hm0360_base_config[][2];
extern const uint16_t himax_qqvga_regs[][2];
extern const uint16_t himax_qvga_regs[][2];
extern const uint16_t himax_vga_regs[][2];


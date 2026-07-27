#include <eff.h>
#include <stdint.h>

#include "hm0360.h"

#define CAM_I2C_MODULE  I2C_2_1
#define CAM_I2C_ADDR    0x24

const uint16_t hm0360_base_config[][2] = {
    {SW_RESET,              0x00},
    
    /* Monochrome programming register */
    {MONO_MODE,             0x01},  // Mono mode indicator --> Enabled
    {MONO_MODE_ISP,         0x01},  // Mono mode indicator for ISP block --> Enabled
    {MONO_MODE_SEL,         0x00},  // Select mono mode indicator from OTP --> Disabled
    {MONO_CTRL,             0x02},  // Mono control --> Enabled
    
    /* Black level control register */
    {0x1000,                0x01},  // Reserved. Set to 1
    {0x1003,                0x04},  // Reserved. Set to the same level as BLC target
    {BLC_TGT,               0x04},  // Black level target --> 4
    {0x1007,                0x01},  // Reserved. Set to 1
    {0x1008,                0x04},  // Reserved. Set to the same level as BLC target
    {BLC2_TGT,              0x04},  // Black level target 2. Set to the same as Black level target --> 4
    
    /* Output format control */
    {OPFM_CTRL,             0x0F},  // Output format control: PCLKO gated, HSYNC/VSYNC shift enable
    
    /* Reserved registers */
    {0x101D,                0x00},
    {0x101E,                0x01},
    {0x101F,                0x00},
    {0x1020,                0x01},
    {0x1021,                0x00},
    
    /* Tone mapping register */
    {CMPRS_CTRL,            0x00},
    {CMPRS_01,              0x09},
    {CMPRS_02,              0x12},
    {CMPRS_03,              0x23},
    {CMPRS_04,              0x31},
    {CMPRS_05,              0x3E},
    {CMPRS_06,              0x4B},
    {CMPRS_07,              0x56},
    {CMPRS_08,              0x5E},
    {CMPRS_09,              0x65},
    {CMPRS_10,              0x72},
    {CMPRS_11,              0x7F},
    {CMPRS_12,              0x8C},
    {CMPRS_13,              0x98},
    {CMPRS_14,              0xB2},
    {CMPRS_15,              0xCC},
    {CMPRS_16,              0xE6},
    
    /* IO and clock control register */
    {VSYNC_FRONT,           0x02},
    {VSYNC_END,             0x02},
    {HSYNC_FRONT_H,         0x00},
    {HSYNC_FRONT_L,         0x0C},  // Move FVLD and LVLD before PCLK begin
    {HSYNC_END_H,           0x00},
    {HSYNC_END_L,           0x02},
    {PCLKO_GATED_EN,        0x02},  // Gate PCLKO by line
    {PCLKO_FRAME_FRONT,     0x02},
    {PCLKO_FRAME_END,       0x02},
    {PCLKO_LINE_FRONT_H,    0x00},
    {PCLKO_LINE_FRONT_L,    0x08},  // Send 16 posedges before data
    {PCLKO_LINE_END_H,      0x00},
    {PCLKO_LINE_END_L,      0x20},
    {OUTPUT_EN,             0x04},
    {FRAME_OUTPUT_EN,       0x01},
    {MULTI_CAMERA_CONFIG,   0x00},
    {MULTI_CAMERA_TUNE_H,   0x02},
    {MULTI_CAMERA_TUNE_L,   0x34},
    {ANA_REGISTER_04,       0x40},  // 4 bit data interface
    {ANA_REGISTER_07,       0x00},  // PCLKO polarity falling
    {PLL_POST_DIV_D,        0x57},
    
    /* Clock control register */
    {PLL1_CONFIG,           0x05},  // Core = 24MHz PCLKO = 24MHz I2C = 12MHz
    {PLL2_CONFIG,           0x0A},  // MIPI pre-div (default)
    {PLL3_CONFIG,           0x46},  // Better setting for slower MCLK
    
    /* Context switch control register */
    {PMU_CFG_3,             0x08},  // Disable context switching
    
    /* Automatic exposure programming register */
    {AE_CTRL,               0x00},  // Automatic Exposure Control --> Disable auto exposure
    
    /* Motion detection control register */
    {MD_CTRL,               0x00},  // Disable motion detection
    
    /* Sub-sampling / Binning control register */
    {H_SUBSAMPLE,           0x00},  // Full Frame
    {V_SUBSAMPLE,           0x00},  // Full Frame
    {BINNING_MODE,          0x00},  // Binning disabled

    /* Sensor mode control register */
    {IMG_ORIENTATION,       0x00},  // Horizontal and vertical flip disabled

    /* Sensor exposure gain control */
    {ANALOG_GAIN,           0x10},
    {DIGITAL_GAIN_H,        0x01},
    {DIGITAL_GAIN_L,        0x00},
    {INTEGRATION_H,         0x02},
    {INTEGRATION_L,         0x68},
    
    /* SYNC function control register */
    {EXP_SYNC_CFG,          0x00},
    {ERR_FLAG_CFG,          0x01},
    {OFFSET_RDSYNC_H,       0x00},
    {OFFSET_RDSYNC_L,       0x00},
    {RDSYNC_DEC_TH_H,       0x20},
    {RDSYNC_DEC_TH_L,       0xFF},
    
    /* Operation mode register */
    {PMU_CFG_5,             0x03},  // Premeter enable at every wakeup and at power up
    {PMU_CFG_6,             0x81},
    {PMU_CFG_7,             0x01},
    {PMU_CFG_8,             0x00},
    {PMU_CFG_9,             0x30},

    /* ROI and sensor control register */
    {WIN_MODE,              0x01},  // Pixel window 640 x 480
    {ROI_CFG,               0x00},  // Disable ROI
    
    /* Strobe Control register */
    {STROBE_CFG,            0x00},  // Disable strobe function
    
    /* Context switch A register */
    {CTX_A_AE_CTRL,         0x00},

    /* Context switch B register */
    {CTX_B_AE_CTRL,         0x00},
    
    /* Magic register */
    {0x302E,                0x00},
    {0x302F,                0x00},
    {0x302B,                0x2A},
    {0x302C,                0x00},
    {0x302D,                0x03},
    {0x3031,                0x01},
    {0x3051,                0x00},
    {0x305C,                0x03},
    {0x3061,                0xFA},
    {0x3062,                0xFF},
    {0x3063,                0xFF},
    {0x3064,                0xFF},
    {0x3065,                0xFF},
    {0x3066,                0xFF},
    {0x3067,                0xFF},
    {0x3068,                0xFF},
    {0x3069,                0xFF},
    {0x306A,                0xFF},
    {0x306B,                0xFF},
    {0x306C,                0xFF},
    {0x306D,                0xFF},
    {0x306E,                0xFF},
    {0x306F,                0xFF},
    {0x3070,                0xFF},
    {0x3071,                0xFF},
    {0x3072,                0xFF},
    {0x3073,                0xFF},
    {0x3074,                0xFF},
    {0x3075,                0xFF},
    {0x3076,                0xFF},
    {0x3077,                0xFF},
    {0x3078,                0xFF},
    {0x3079,                0xFF},
    {0x307A,                0xFF},
    {0x307B,                0xFF},
    {0x307C,                0xFF},
    {0x307D,                0xFF},
    {0x307E,                0xFF},
    {0x307F,                0xFF},
    {0x30A6,                0x02},
    {0x30A7,                0x02},
    {0x30B0,                0x03},
    {0x30C4,                0x10},
    {0x30C5,                0x01},
    {0x30C6,                0xBF},
    {0x30C7,                0x00},
    {0x30C8,                0x00},
    {0x30CB,                0xFF},
    {0x30CC,                0xFF},
    {0x30CD,                0x7F},
    {0x30CE,                0x7F},
    {0x30D3,                0x01},
    {0x30D4,                0xFF},
    {0x30D5,                0x00},
    {0x30D6,                0x40},
    {0x30D7,                0x00},
    {0x30D8,                0xA7},
    {0x30D9,                0x05},
    {0x30DA,                0x01},
    {0x30DB,                0x40},
    {0x30DC,                0x00},
    {0x30DD,                0x27},
    {0x30DE,                0x05},
    {0x30DF,                0x07},
    {0x30E0,                0x40},
    {0x30E1,                0x00},
    {0x30E2,                0x27},
    {0x30E3,                0x05},
    {0x30E4,                0x47},
    {0x30E5,                0x30},
    {0x30E6,                0x00},
    {0x30E7,                0x27},
    {0x30E8,                0x05},
    {0x30E9,                0x87},
    {0x30EA,                0x30},
    {0x30EB,                0x00},
    {0x30EC,                0x27},
    {0x30ED,                0x05},
    {0x30EE,                0x00},
    {0x30EF,                0x40},
    {0x30F0,                0x00},
    {0x30F1,                0xA7},
    {0x30F2,                0x05},
    {0x30F3,                0x01},
    {0x30F4,                0x40},
    {0x30F5,                0x00},
    {0x30F6,                0x27},
    {0x30F7,                0x05},
    {0x30F8,                0x07},
    {0x30F9,                0x40},
    {0x30FA,                0x00},
    {0x30FB,                0x27},
    {0x30FC,                0x05},
    {0x30FD,                0x47},
    {0x30FE,                0x30},
    {0x30FF,                0x00},
    {0x3100,                0x27},
    {0x3101,                0x05},
    {0x3102,                0x87},
    {0x3103,                0x30},
    {0x3104,                0x00},
    {0x3105,                0x27},
    {0x3106,                0x05},
    {0x310B,                0x10},
    {0x3113,                0xA0},
    {0x3114,                0x67},
    {0x3115,                0x42},
    {0x3116,                0x10},
    {0x3117,                0x0A},
    {0x3118,                0x3F},
    {0x311C,                0x10},
    {0x311D,                0x06},
    {0x311E,                0x0F},
    {0x311F,                0x0E},
    {0x3120,                0x0D},
    {0x3121,                0x0F},
    {0x3122,                0x00},
    {0x3123,                0x1D},
    {0x3126,                0x03},
    {0x312A,                0x11},
    {0x312B,                0x41},
    {0x312E,                0x00},
    {0x312F,                0x00},
    {0x3130,                0x0C},
    {0x3141,                0x2A},
    {0x3142,                0x9F},
    {0x3147,                0x18},
    {0x3149,                0x18},
    {0x314B,                0x01},
    {0x3150,                0x50},
    {0x3152,                0x00},
    {0x3156,                0x2C},
    {0x315A,                0x0A},
    {0x315B,                0x2F},
    {0x315C,                0xE0},
    {0x315F,                0x02},
    {0x3160,                0x1F},
    {0x3163,                0x1F},
    {0x3164,                0x7F},
    {0x3165,                0x7F},
    {0x317B,                0x94},
    {0x317C,                0x00},
    {0x317D,                0x02},
    {0x318C,                0x00},
    {0x35C6,                0x00},
    {0x303E,                0x00},

    {COMMAND_UPDATE,        0x01},
    {0x0000,                0x00},
};

const uint16_t himax_qqvga_regs[][2] = {
    {H_SUBSAMPLE,           0x02},
    {V_SUBSAMPLE,           0x02},
    {MAX_INTG_H,            (HIMAX_FRAME_LENGTH_QQVGA-4)>>8},
    {MAX_INTG_L,            (HIMAX_FRAME_LENGTH_QQVGA-4)&0xFF},
    {FRAME_LEN_LINES_H,     (HIMAX_FRAME_LENGTH_QQVGA>>8)},
    {FRAME_LEN_LINES_L,     (HIMAX_FRAME_LENGTH_QQVGA&0xFF)},
    {LINE_LEN_PCK_H,        (HIMAX_LINE_LEN_PCK_QQVGA>>8)},
    {LINE_LEN_PCK_L,        (HIMAX_LINE_LEN_PCK_QQVGA&0xFF)},
    {ROI_START_END_H,       0xF0},
    {ROI_START_END_V,       0xD0},
    {COMMAND_UPDATE,        0x01},
    {0x0000,                0x00},
};

const uint16_t himax_qvga_regs[][2] = {
    {H_SUBSAMPLE,           0x01},
    {V_SUBSAMPLE,           0x01},
    {MAX_INTG_H,            (HIMAX_FRAME_LENGTH_QVGA-4)>>8},
    {MAX_INTG_L,            (HIMAX_FRAME_LENGTH_QVGA-4)&0xFF},
    {FRAME_LEN_LINES_H,     (HIMAX_FRAME_LENGTH_QVGA>>8)},
    {FRAME_LEN_LINES_L,     (HIMAX_FRAME_LENGTH_QVGA&0xFF)},
    {LINE_LEN_PCK_H,        (HIMAX_LINE_LEN_PCK_QVGA>>8)},
    {LINE_LEN_PCK_L,        (HIMAX_LINE_LEN_PCK_QVGA&0xFF)},
    {ROI_START_END_H,       0xF0},
    {ROI_START_END_V,       0xE0},
    {COMMAND_UPDATE,        0x01},
    {0x0000,                0x00},
};

const uint16_t himax_vga_regs[][2] = {
    {H_SUBSAMPLE,           0x00},
    {V_SUBSAMPLE,           0x00},
    {MAX_INTG_H,            (HIMAX_FRAME_LENGTH_VGA-4)>>8},
    {MAX_INTG_L,            (HIMAX_FRAME_LENGTH_VGA-4)&0xFF},
    {FRAME_LEN_LINES_H,     (HIMAX_FRAME_LENGTH_VGA>>8)},
    {FRAME_LEN_LINES_L,     (HIMAX_FRAME_LENGTH_VGA&0xFF)},
    {LINE_LEN_PCK_H,        (HIMAX_LINE_LEN_PCK_VGA>>8)},
    {LINE_LEN_PCK_L,        (HIMAX_LINE_LEN_PCK_VGA&0xFF)},
    {ROI_START_END_H,       0xF0},
    {ROI_START_END_V,       0xE0},
    {COMMAND_UPDATE,        0x01},
    {0x0000,                0x00},
};

int8_t hm0360_write_reg(uint16_t reg_addr, uint8_t reg_val) {
    uint8_t v = reg_val;
    return eff_i2c_write_wide(CAM_I2C_MODULE, CAM_I2C_ADDR, reg_addr, &v, 1);
}

int8_t hm0360_read_reg(uint16_t reg_addr, uint8_t *reg_val) {
    return eff_i2c_read_wide(CAM_I2C_MODULE, CAM_I2C_ADDR, reg_addr, reg_val, 1);
}

int8_t hm0360_interface_init(hm0360_interface_t *interface) {
    // Init camera GPIOs
    eff_pinmux_set(interface->enable_pinmux, PINMUX_GPIO);
    eff_gpio_dir_set(interface->enable_gpio, interface->enable_pin, EFF_GPIO_OUT);

    eff_pinmux_set(interface->pwdn_pinmux, PINMUX_GPIO);
    eff_gpio_dir_set(interface->pwdn_gpio, interface->pwdn_pin, EFF_GPIO_OUT);

    eff_pinmux_set(interface->vsync_pinmux, PINMUX_GPIO);
    eff_gpio_dir_set(interface->vsync_gpio, interface->vsync_pin, EFF_GPIO_IN);
    eff_gpio_pull_set(interface->vsync_gpio, interface->vsync_pin, EFF_GPIO_PULL_NONE);

    // Init camera I2C
    //eff_pinmux_set(interface->i2c, PINMUX_I2C
}


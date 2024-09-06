/*
 * Copyright © 2023 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __INTEL_DSI_SER_DEV_h__
#define __INTEL_DSI_SER_DEV_h__

#define DEBUG

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#define MAX_DSI_SER_TX_ADD                  0x40
#define MAX_DSI_SER_RX_ADD_A                0x4A
#define MAX_DSI_SER_RX_ADD_B                0x48
#define MAX_DSI_ARRAY_SIZE                  4

#define MAX_DSI_SER_CTRL3                   0x13
#define MAX_DSI_SER_CTRL3_LOCK_MASK         (1 << 3)
#define MAX_DSI_SER_CTRL3_LOCK_VAL          (1 << 3)

#define MAX_DSI_SER_LCTRL2_A                0x2A
#define MAX_DSI_SER_LCTRL2_B                0x34
#define MAX_DSI_SER_LCTRL2_LOCK_MASK        (1 << 0)
#define MAX_DSI_SER_LCTRL2_LOCK_VAL         0x1

#define MAX_DSI_SER_VID_TX_MASK             (1 << 0)
#define MAX_DSI_SER_VID_TX_LINK_MASK        (3 << 1)
#define MAX_DSI_SER_LINK_SEL_SHIFT_VAL      0x1

#define MAX_DSI_SER_DPRX_TRAIN              0x641A
#define MAX_DSI_SER_DPRX_TRAIN_STATE_MASK   (0xF << 4)
#define MAX_DSI_SER_DPRX_TRAIN_STATE_VAL    0xF0

#define MAX_DSI_SER_LINK_CTRL_PHY_A         0x29
#define MAX_DSI_SER_LINK_CTRL_A_MASK        (1 << 0)

#define MAX_DSI_SER_LCTRL2_A                0x2A
#define MAX_DSI_SER_LCTRL2_B                0x34
#define MAX_DSI_SER_LCTRL2_LOCK_MASK        (1 << 0)
#define MAX_DSI_SER_LCTRL2_LOCK_VAL         0x1

#define MAX_DSI_SER_LINK_CTRL_PHY_B         0x33
#define MAX_DSI_SER_LINK_CTRL_B_MASK        (1 << 0)

#define MAX_DSI_SER_PCLK                    0x102
#define MAX_DSI_SER_PCLK_LOCK_MASK          (0x45 << 1)
#define MAX_DSI_SER_PCLK_LOCK_VAL           0x8A

#define MAX_DSI_SER_ERR                     0x3A0
#define MAX_DSI_SER_ERR_LOCK_MASK           0xFF
#define MAX_DSI_SER_ERR_LOCK_VAL            0x00

#define MAX_DSI_SER_HS_VS                   0x55D
#define MAX_DSI_SER_HS_VS_LOCK_MASK         0x73
#define MAX_DSI_SER_HS_VS_LOCK_VAL          0x73

#define MAX_DP_DESER_VID                    0x1DC
#define MAX_DP_DESER_VID_LOCK_MASK          (1 << 0)
#define MAX_DP_DESER_VID_LOCK_VAL           0x1

#define MAX_DP_DESER_SS_B0                  0x7F0
#define MAX_DP_DESER_SS_B0_LOCK_MASK        (1 << 0)
#define MAX_DP_DESER_SS_B0_LOCK_VAL         0x1

#define MAX_DP_DESER_SS_B1                  0x7F1
#define MAX_DP_DESER_SS_B1_LOCK_MASK        0xFF
#define MAX_DP_DESER_SS_B1_LOCK_VAL         0x00

#define MAX_DSI_SER_VID_TX_X                0x100
#define MAX_DSI_SER_VID_TX_Y                0x110
#define MAX_DSI_SER_VID_TX_Z                0x120
#define MAX_DSI_SER_VID_TX_U                0x130

#define MAX96789                           0
#define MAX96772A                          1
#define MAX96772B                          2

#define NUM_DSI_DEVICE                     3
#define BUS_DSI_NUMBER                     2

enum max_dsi_ser_current_mode {
    MAX_MODE_DSI_480,
    MAX_MODE_DSI_768,
    MAX_MODE_DSI_720P,
    MAX_MODE_DSI_1080P,
};

struct max_dsi_ser_priv {
    struct gpio_desc *gpiod_pwrdn;
    u8 dprx_lane_count;
    u8 dprx_link_rate;
    struct mutex mutex;
    int ser_errb;
    unsigned int ser_irq;
    bool enable_mst;
    u8 mst_payload_ids[MAX_DSI_ARRAY_SIZE];
    u8 gmsl_stream_ids[MAX_DSI_ARRAY_SIZE];
    u8 gmsl_link_select[MAX_DSI_ARRAY_SIZE];
    bool link_a_is_enabled;
    bool link_b_is_enabled;
    int current_mode;
    bool dsc;
    bool split_mode;
    struct i2c_client *priv_dsi_client[NUM_DSI_DEVICE];
    struct delayed_work delay_work;
    struct workqueue_struct *wq;
    struct delayed_work deser_work;
    struct workqueue_struct *deser_wq;
};

void intel_dsi_ser_module_exit(void);
int intel_dsi_ser_module_init(u16 crtc_hdisplay, u16 crtc_vdisplay);

void intel_dsi_ser_exit(void);
int intel_dsi_ser_init(void);

#endif /* __INTEL_DSI_SER_DRV__ */

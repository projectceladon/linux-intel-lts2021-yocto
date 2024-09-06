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

#include "intel_dsi_ser_drv.h"

struct i2c_client       *max_dsi_client[MAX_DSI_ARRAY_SIZE];
struct max_dsi_ser_priv *max_dsi_priv;
struct i2c_client       *priv_dsi_client[MAX_DSI_ARRAY_SIZE];
u16 hdisplay = 0;
u16 vdisplay = 0;

static struct i2c_board_info max_dsi_i2c_board_info[] = {
    {
        I2C_BOARD_INFO("max96789", MAX_DSI_SER_TX_ADD),
    },
    {
        I2C_BOARD_INFO("max96772A", MAX_DSI_SER_RX_ADD_A),
    },
    {
        I2C_BOARD_INFO("max96772B", MAX_DSI_SER_RX_ADD_B),
    },
};

char max_dsi_ser_read_reg(struct i2c_client *client, unsigned int reg_addr, u8 *val)
{
    u8 buf[2];
    int ret = 0;

    struct i2c_msg msg[2];

    buf[0] = reg_addr >> 8;
    buf[1] = reg_addr & 0xff;

    msg[0].addr = client->addr;
    msg[0].flags = client->flags;
    msg[0].buf = buf;
    msg[0].len = sizeof(buf);

    msg[1].addr = client->addr;
    msg[1].flags = client->flags | I2C_M_RD;
    msg[1].buf = val;
    msg[1].len = 1;

    i2c_transfer(client->adapter, msg, 2);
    if (ret < 0) {
        pr_debug("MAX_DSI [-%s-%s-%d-], fail reg_addr=0x%x, val=%u\n",
               __FILE__, __func__, __LINE__, reg_addr, *val);
        return -ENODEV;
    } else {
        pr_debug("MAX_DSI 0x%02x, 0x%04x,0x%02x\n", client->addr,reg_addr, *val);
    }
    return 0;
}

void max_dsi_ser_write_reg(struct i2c_client *client, unsigned int reg_addr, unsigned int val)
{
    int ret= 0;
    struct i2c_msg msg;
    u8 buf[3];
    u8 read_val;

    buf[0] = (reg_addr&0xff00) >> 8;
    buf[1] = reg_addr & 0xff;
    buf[2] = val;

    msg.addr = client->addr;
    msg.flags = client->flags;
    msg.buf = buf;
    msg.len = sizeof(buf);

    ret=i2c_transfer(client->adapter, &msg, 1);
    if (ret < 0) {
        pr_debug("MAX_DSI [-%s-%s-%d-], fail client->addr=0x%x, reg_addr=0x%x, val=0x%x\n",
         __FILE__, __func__, __LINE__, client->addr, reg_addr, val);
    }  else {
        max_dsi_ser_read_reg(client, reg_addr, &read_val);
    }
}

void max_dsi_ser_update(struct i2c_client *client,
        u32 reg, u32 mask, u8 val)
{
    u8 update_val;

    max_dsi_ser_read_reg(client, reg, &update_val);
    update_val = ((update_val & (~mask)) | (val & mask));
    max_dsi_ser_write_reg(client, reg, update_val);
}

int max_dsi_ser_prepare(void)
{
    pr_debug("MAX_DSI %s: hdisplay =%d, vdisplay = %d\n", __func__, hdisplay, vdisplay);

    if ((hdisplay == 640) && (vdisplay == 480))
    {
        max_dsi_priv->current_mode   = MAX_MODE_DSI_480;
        max_dsi_priv->split_mode     = false;
    }
    else if ((hdisplay == 1280) && (vdisplay == 480))
    {
        max_dsi_priv->current_mode   = MAX_MODE_DSI_480;
        max_dsi_priv->split_mode     = true;
    }
    else if ((hdisplay == 1280) && (vdisplay == 720))
    {
        max_dsi_priv->current_mode   = MAX_MODE_DSI_720P;
        max_dsi_priv->split_mode     = false;
    }
    else if ((hdisplay == 2560) && (vdisplay == 720))
    {
        max_dsi_priv->current_mode   = MAX_MODE_DSI_720P;
        max_dsi_priv->split_mode     = true;
    }
    else if ((hdisplay == 1920) && (vdisplay == 1080))
    {
        max_dsi_priv->current_mode   = MAX_MODE_DSI_1080P;
        max_dsi_priv->split_mode     = false;
    }
    else if ((hdisplay == 3840) && (vdisplay == 1080))
    {
        max_dsi_priv->current_mode   = MAX_MODE_DSI_1080P;
        max_dsi_priv->split_mode     = true;
    }

    pr_debug("MAX_DSI %s: current_mode =%d, split_mode = %d\n", __func__, max_dsi_priv->current_mode, max_dsi_priv->split_mode);

    return 0;
}

int max_dsi_ser_setup(struct i2c_client *client)
{
    // Max96789 splitter mode
    // Write TX Enable Y
    max_dsi_ser_write_reg(client, 0x02, 0x73);
    // Set Stream ID = 0 for GMSL PHY A
    max_dsi_ser_write_reg(client, 0x53, 0x10);
    //Set Stream ID = 1 for GMSL PHY B
    max_dsi_ser_write_reg(client, 0x57, 0x21);
    // Set Port A Lane Mapping
    max_dsi_ser_write_reg(client, 0x332, 0x4E);
    // Set Port B Lane Mapping
    max_dsi_ser_write_reg(client, 0x333, 0xE4);
    // Clock Select
    max_dsi_ser_write_reg(client, 0x308, 0x5C);
    // Start DSI Port
    max_dsi_ser_write_reg(client, 0x311, 0x03);
    // Number of Lanes
    max_dsi_ser_write_reg(client, 0x331, 0x03);
    // Set phy_config
    max_dsi_ser_write_reg(client, 0x330, 0x06);
    // Set soft_dtx_en
    max_dsi_ser_write_reg(client, 0x31C, 0x98);
    // Set soft_dtx
    max_dsi_ser_write_reg(client, 0x321, 0x24);
    // Set soft_dty_en
    max_dsi_ser_write_reg(client, 0x31D, 0x98);
    // Set soft_dty_
    max_dsi_ser_write_reg(client, 0x322, 0x24);
    // Enable Dual View Block Port A
    max_dsi_ser_write_reg(client, 0x32A, 0x07);
    // Video Pipe Enable
    max_dsi_ser_write_reg(client, 0x02, 0x73);

    // Enable splitter mode, reset one shot
    if (max_dsi_priv->split_mode == true)
        max_dsi_ser_write_reg(client, 0x10, 0x23);
    else
        max_dsi_ser_write_reg(client, 0x10, 0x21);

    return 0;
}

int max_dsi_deser_setup(struct i2c_client *client)
{
    switch (max_dsi_priv->current_mode) {
        case MAX_MODE_DSI_480:
            // Link Rate
            max_dsi_ser_write_reg(client, 0xE790, 0x0A);
            max_dsi_ser_write_reg(client, 0xE791, 0x00);
            // Lane Count
            max_dsi_ser_write_reg(client, 0xE792, 0x04);
            max_dsi_ser_write_reg(client, 0xE793, 0x00);
            // Hres
            max_dsi_ser_write_reg(client, 0xE794, 0x80);
            max_dsi_ser_write_reg(client, 0xE795, 0x02);
            // Hfp
            max_dsi_ser_write_reg(client, 0xE796, 0x10);
            max_dsi_ser_write_reg(client, 0xE797, 0x00);
            // Hsw
            max_dsi_ser_write_reg(client, 0xE798, 0x60);
            max_dsi_ser_write_reg(client, 0xE799, 0x00);
            // Hbp
            max_dsi_ser_write_reg(client, 0xE79A, 0x30);
            max_dsi_ser_write_reg(client, 0xE79B, 0x00);
            // Vres
            max_dsi_ser_write_reg(client, 0xE79C, 0xE0);
            max_dsi_ser_write_reg(client, 0xE79D, 0x01);
            // Vfp
            max_dsi_ser_write_reg(client, 0xE79E, 0x0A);
            max_dsi_ser_write_reg(client, 0xE79F, 0x00);
            // Vsw
            max_dsi_ser_write_reg(client, 0xE7A0, 0x02);
            max_dsi_ser_write_reg(client, 0xE7A1, 0x00);
            // Vbp
            max_dsi_ser_write_reg(client, 0xE7A2, 0x21);
            max_dsi_ser_write_reg(client, 0xE7A3, 0x00);
            // Hwords
            max_dsi_ser_write_reg(client, 0xE7A4, 0xBC);
            max_dsi_ser_write_reg(client, 0xE7A5, 0x03);
            // Mvid PCLK
            max_dsi_ser_write_reg(client, 0xE7A6, 0xF2);
            max_dsi_ser_write_reg(client, 0xE7A7, 0x0B);
            // Nvid Line Rate
            max_dsi_ser_write_reg(client, 0xE7A8, 0x00);
            max_dsi_ser_write_reg(client, 0xE7A9, 0x80);
            // TUC_Value
            max_dsi_ser_write_reg(client, 0xE7AA, 0x40);
            max_dsi_ser_write_reg(client, 0xE7AB, 0x00);
            // HVPOL
            max_dsi_ser_write_reg(client, 0xE7AC, 0x01);
            max_dsi_ser_write_reg(client, 0xE7AD, 0x01);
            // SSC Enable
            max_dsi_ser_write_reg(client, 0xE7B0, 0x01);
            max_dsi_ser_write_reg(client, 0xE7B1, 0x00);
            // Spread Bit Ratio
            max_dsi_ser_write_reg(client, 0x6003, 0x82);
            // CLK_REF_BLOCK
            max_dsi_ser_write_reg(client, 0xE7B2, 0x50);
            max_dsi_ser_write_reg(client, 0xE7B3, 0x00);
            max_dsi_ser_write_reg(client, 0xE7B4, 0x00);
            max_dsi_ser_write_reg(client, 0xE7B5, 0x40);
            max_dsi_ser_write_reg(client, 0xE7B6, 0x6C);
            max_dsi_ser_write_reg(client, 0xE7B7, 0x20);
            max_dsi_ser_write_reg(client, 0xE7B8, 0x07);
            max_dsi_ser_write_reg(client, 0xE7B9, 0x00);
            max_dsi_ser_write_reg(client, 0xE7BA, 0x01);
            max_dsi_ser_write_reg(client, 0xE7BB, 0x00);
            max_dsi_ser_write_reg(client, 0xE7BC, 0x00);
            max_dsi_ser_write_reg(client, 0xE7BD, 0x00);
            max_dsi_ser_write_reg(client, 0xE7BE, 0x52);
            max_dsi_ser_write_reg(client, 0xE7BF, 0x00);
            // Send eDP Controller Command - Start Link Training
            max_dsi_ser_write_reg(client, 0xE776, 0x02);
            max_dsi_ser_write_reg(client, 0xE777, 0x80);
            break;
    case MAX_MODE_DSI_768:
        // Link Rate
        max_dsi_ser_write_reg(client, 0xE790, 0x0A);
        max_dsi_ser_write_reg(client, 0xE791, 0x00);
        // Lane Count
        max_dsi_ser_write_reg(client, 0xE792, 0x04);
        max_dsi_ser_write_reg(client, 0xE793, 0x00);
        // Hres
        max_dsi_ser_write_reg(client, 0xE794, 0x00);
        max_dsi_ser_write_reg(client, 0xE795, 0x04);
        // Hfp
        max_dsi_ser_write_reg(client, 0xE796, 0x18);
        max_dsi_ser_write_reg(client, 0xE797, 0x00);
        // Hsw
        max_dsi_ser_write_reg(client, 0xE798, 0x88);
        max_dsi_ser_write_reg(client, 0xE799, 0x00);
        // Hbp
        max_dsi_ser_write_reg(client, 0xE79A, 0xA0);
        max_dsi_ser_write_reg(client, 0xE79B, 0x00);
        // Vres
        max_dsi_ser_write_reg(client, 0xE79C, 0x00);
        max_dsi_ser_write_reg(client, 0xE79D, 0x03);
        // Vfp
        max_dsi_ser_write_reg(client, 0xE79E, 0x03);
        max_dsi_ser_write_reg(client, 0xE79F, 0x00);
        // Vsw
        max_dsi_ser_write_reg(client, 0xE7A0, 0x06);
        max_dsi_ser_write_reg(client, 0xE7A1, 0x00);
        // Vbp
        max_dsi_ser_write_reg(client, 0xE7A2, 0x1D);
        max_dsi_ser_write_reg(client, 0xE7A3, 0x00);
        // Hwords
        max_dsi_ser_write_reg(client, 0xE7A4, 0xFC);
        max_dsi_ser_write_reg(client, 0xE7A5, 0x05);
        // Mvid PCLK
        max_dsi_ser_write_reg(client, 0xE7A6, 0xD0);
        max_dsi_ser_write_reg(client, 0xE7A7, 0x1E);
        // Nvid Line Rate
        max_dsi_ser_write_reg(client, 0xE7A8, 0x00);
        max_dsi_ser_write_reg(client, 0xE7A9, 0x80);
        // TUC_Value
        max_dsi_ser_write_reg(client, 0xE7AA, 0x40);
        max_dsi_ser_write_reg(client, 0xE7AB, 0x00);
        // HVPOL
        max_dsi_ser_write_reg(client, 0xE7AC, 0x00);
        max_dsi_ser_write_reg(client, 0xE7AD, 0x00);
        // SSC Enable
        max_dsi_ser_write_reg(client, 0xE7B0, 0x01);
        max_dsi_ser_write_reg(client, 0xE7B1, 0x00);
        // Spread Bit Ratio
        max_dsi_ser_write_reg(client, 0x6003, 0x82);
        // CLK_REF_BLOCK
        max_dsi_ser_write_reg(client, 0xE7B2, 0x50);
        max_dsi_ser_write_reg(client, 0xE7B3, 0x00);
        max_dsi_ser_write_reg(client, 0xE7B4, 0x00);
        max_dsi_ser_write_reg(client, 0xE7B5, 0x40);
        max_dsi_ser_write_reg(client, 0xE7B6, 0x6C);
        max_dsi_ser_write_reg(client, 0xE7B7, 0x20);
        max_dsi_ser_write_reg(client, 0xE7B8, 0x07);
        max_dsi_ser_write_reg(client, 0xE7B9, 0x00);
        max_dsi_ser_write_reg(client, 0xE7BA, 0x01);
        max_dsi_ser_write_reg(client, 0xE7BB, 0x00);
        max_dsi_ser_write_reg(client, 0xE7BC, 0x00);
        max_dsi_ser_write_reg(client, 0xE7BD, 0x00);
        max_dsi_ser_write_reg(client, 0xE7BE, 0x52);
        max_dsi_ser_write_reg(client, 0xE7BF, 0x00);
        // Send eDP Controller Command - Start Link Training
        max_dsi_ser_write_reg(client, 0xE776, 0x02);
        max_dsi_ser_write_reg(client, 0xE777, 0x80);
        break;
    case MAX_MODE_DSI_720P:
        // Link Rate
        max_dsi_ser_write_reg(client, 0xE790, 0x0A);
        max_dsi_ser_write_reg(client, 0xE791, 0x00);
        // Lane Count
        max_dsi_ser_write_reg(client, 0xE792, 0x04);
        max_dsi_ser_write_reg(client, 0xE793, 0x00);
        // Hres
        max_dsi_ser_write_reg(client, 0xE794, 0x00);
        max_dsi_ser_write_reg(client, 0xE795, 0x05);
        // Hfp
        max_dsi_ser_write_reg(client, 0xE796, 0x6E);
        max_dsi_ser_write_reg(client, 0xE797, 0x00);
        // Hsw
        //max_dsi_ser_write_reg(client, 0xE798, 0x28);
        //max_dsi_ser_write_reg(client, 0xE799, 0x00);
        max_dsi_ser_write_reg(client, 0xE798, 0x1E);
        max_dsi_ser_write_reg(client, 0xE799, 0x00);
        // Hbp
        //max_dsi_ser_write_reg(client, 0xE79A, 0xDC);
        //max_dsi_ser_write_reg(client, 0xE79B, 0x00);
        max_dsi_ser_write_reg(client, 0xE79A, 0xE6);
        max_dsi_ser_write_reg(client, 0xE79B, 0x00);
        // Vres
        max_dsi_ser_write_reg(client, 0xE79C, 0xD0);
        max_dsi_ser_write_reg(client, 0xE79D, 0x02);
        // Vfp
        max_dsi_ser_write_reg(client, 0xE79E, 0x05);
        max_dsi_ser_write_reg(client, 0xE79F, 0x00);
        // Vsw
        max_dsi_ser_write_reg(client, 0xE7A0, 0x05);
        max_dsi_ser_write_reg(client, 0xE7A1, 0x00);
        // Vbp
        max_dsi_ser_write_reg(client, 0xE7A2, 0x14);
        max_dsi_ser_write_reg(client, 0xE7A3, 0x00);
        // Hwords
        max_dsi_ser_write_reg(client, 0xE7A4, 0x7C);
        max_dsi_ser_write_reg(client, 0xE7A5, 0x07);
        // Mvid PCLK
        max_dsi_ser_write_reg(client, 0xE7A6, 0x51);
        max_dsi_ser_write_reg(client, 0xE7A7, 0x23);
        // Nvid Line Rate
        max_dsi_ser_write_reg(client, 0xE7A8, 0x00);
        max_dsi_ser_write_reg(client, 0xE7A9, 0x80);
        // TUC_Value
        max_dsi_ser_write_reg(client, 0xE7AA, 0x40);
        max_dsi_ser_write_reg(client, 0xE7AB, 0x00);
        // HVPOL
        max_dsi_ser_write_reg(client, 0xE7AC, 0x00);
        max_dsi_ser_write_reg(client, 0xE7AD, 0x00);
        // SSC Enable
        max_dsi_ser_write_reg(client, 0xE7B0, 0x01);
        max_dsi_ser_write_reg(client, 0xE7B1, 0x00);
        // Spread Bit Ratio
        max_dsi_ser_write_reg(client, 0x6003, 0x82);
        // CLK_REF_BLOCK
        max_dsi_ser_write_reg(client, 0xE7B2, 0x50);
        max_dsi_ser_write_reg(client, 0xE7B3, 0x00);
        max_dsi_ser_write_reg(client, 0xE7B4, 0x00);
        max_dsi_ser_write_reg(client, 0xE7B5, 0x40);
        max_dsi_ser_write_reg(client, 0xE7B6, 0x6C);
        max_dsi_ser_write_reg(client, 0xE7B7, 0x20);
        max_dsi_ser_write_reg(client, 0xE7B8, 0x07);
        max_dsi_ser_write_reg(client, 0xE7B9, 0x00);
        max_dsi_ser_write_reg(client, 0xE7BA, 0x01);
        max_dsi_ser_write_reg(client, 0xE7BB, 0x00);
        max_dsi_ser_write_reg(client, 0xE7BC, 0x00);
        max_dsi_ser_write_reg(client, 0xE7BD, 0x00);
        max_dsi_ser_write_reg(client, 0xE7BE, 0x52);
        max_dsi_ser_write_reg(client, 0xE7BF, 0x00);
        // Send eDP Controller Command - Start Link Training
        max_dsi_ser_write_reg(client, 0xE776, 0x02);
        max_dsi_ser_write_reg(client, 0xE777, 0x80);
        break;
    case MAX_MODE_DSI_1080P:
    default:
        // Link Rate
        max_dsi_ser_write_reg(client, 0xE790, 0x0A);
        //max_dsi_ser_write_reg(client, 0xE790, 0x06);
        max_dsi_ser_write_reg(client, 0xE791, 0x00);
        // Lane Count
        max_dsi_ser_write_reg(client, 0xE792, 0x04);
        max_dsi_ser_write_reg(client, 0xE793, 0x00);
        // Hres
        max_dsi_ser_write_reg(client, 0xE794, 0x80);
        max_dsi_ser_write_reg(client, 0xE795, 0x07);
        // Hfp
        max_dsi_ser_write_reg(client, 0xE796, 0x58);
        max_dsi_ser_write_reg(client, 0xE797, 0x00);
        // Hsw
        max_dsi_ser_write_reg(client, 0xE798, 0x2C);
        max_dsi_ser_write_reg(client, 0xE799, 0x00);
        // Hbp
        max_dsi_ser_write_reg(client, 0xE79A, 0x94);
        max_dsi_ser_write_reg(client, 0xE79B, 0x00);
        // Vres
        max_dsi_ser_write_reg(client, 0xE79C, 0x38);
        max_dsi_ser_write_reg(client, 0xE79D, 0x04);
        // Vfp
        max_dsi_ser_write_reg(client, 0xE79E, 0x04);
        max_dsi_ser_write_reg(client, 0xE79F, 0x00);
        // Vsw
        max_dsi_ser_write_reg(client, 0xE7A0, 0x05);
        max_dsi_ser_write_reg(client, 0xE7A1, 0x00);
        // Vbp
        max_dsi_ser_write_reg(client, 0xE7A2, 0x24);
        max_dsi_ser_write_reg(client, 0xE7A3, 0x00);
        // Hwords
        max_dsi_ser_write_reg(client, 0xE7A4, 0x3C);
        max_dsi_ser_write_reg(client, 0xE7A5, 0x0B);
        // Mvid PCLK
        max_dsi_ser_write_reg(client, 0xE7A6, 0x66);
        max_dsi_ser_write_reg(client, 0xE7A7, 0x46);
        //max_dsi_ser_write_reg(client, 0xE7A6, 0x55);
        //max_dsi_ser_write_reg(client, 0xE7A7, 0x75);
        // Nvid Line Rate
        max_dsi_ser_write_reg(client, 0xE7A8, 0x00);
        max_dsi_ser_write_reg(client, 0xE7A9, 0x80);
        // TUC_Value
        max_dsi_ser_write_reg(client, 0xE7AA, 0x40);
        max_dsi_ser_write_reg(client, 0xE7AB, 0x00);
        // HVPOL
        max_dsi_ser_write_reg(client, 0xE7AC, 0x00);
        max_dsi_ser_write_reg(client, 0xE7AD, 0x00);
#if 1
        // SSC Enable
        max_dsi_ser_write_reg(client, 0xE7B0, 0x01);
        max_dsi_ser_write_reg(client, 0xE7B1, 0x00);
        // Spread Bit Ratio
        max_dsi_ser_write_reg(client, 0x6003, 0x82);
        // CLK_REF_BLOCK
        max_dsi_ser_write_reg(client, 0xE7B2, 0x50);
        max_dsi_ser_write_reg(client, 0xE7B3, 0x00);
        max_dsi_ser_write_reg(client, 0xE7B4, 0x00);
        max_dsi_ser_write_reg(client, 0xE7B5, 0x40);
        max_dsi_ser_write_reg(client, 0xE7B6, 0x6C);
        max_dsi_ser_write_reg(client, 0xE7B7, 0x20);
        max_dsi_ser_write_reg(client, 0xE7B8, 0x07);
        max_dsi_ser_write_reg(client, 0xE7B9, 0x00);
#endif

#if 0
        // SSC Enable
        max_dsi_ser_write_reg(client, 0xE7B0, 0x01);
        max_dsi_ser_write_reg(client, 0xE7B1, 0x10);
        // Spread Bit Ratio
        max_dsi_ser_write_reg(client, 0x6003, 0x81);
        // CLK_REF_BLOCK
        max_dsi_ser_write_reg(client, 0xE7B2, 0x50);
        max_dsi_ser_write_reg(client, 0xE7B3, 0x00);
        max_dsi_ser_write_reg(client, 0xE7B4, 0x35);
        max_dsi_ser_write_reg(client, 0xE7B5, 0x42);
        max_dsi_ser_write_reg(client, 0xE7B6, 0x81);
        max_dsi_ser_write_reg(client, 0xE7B7, 0x30);
        max_dsi_ser_write_reg(client, 0xE7B8, 0x07);
        max_dsi_ser_write_reg(client, 0xE7B9, 0x10);
#endif

        max_dsi_ser_write_reg(client, 0xE7BA, 0x01);
        max_dsi_ser_write_reg(client, 0xE7BB, 0x00);
        max_dsi_ser_write_reg(client, 0xE7BC, 0x00);
        max_dsi_ser_write_reg(client, 0xE7BD, 0x00);
        max_dsi_ser_write_reg(client, 0xE7BE, 0x52);
        max_dsi_ser_write_reg(client, 0xE7BF, 0x00);
        // Send eDP Controller Command - Start Link Training
        max_dsi_ser_write_reg(client, 0xE776, 0x02);
        max_dsi_ser_write_reg(client, 0xE777, 0x80);
        break;
    }

    return 0;
}

static int max_read_lock(struct i2c_client *client, unsigned int reg_addr,
              u32 mask, u32 expected_value)
{
    u8 reg_data;

    max_dsi_ser_read_reg(client, reg_addr, &reg_data);
    if ((reg_data & mask) == expected_value)
        return 0;

    return -1;
}

void max_dsi_ser_enable(void)
{
    max_dsi_ser_prepare();
    max_dsi_ser_setup(max_dsi_priv->priv_dsi_client[0]);
}

static void max_poll_gmsl_deser_lock(struct work_struct *work)
{
    int ret1_1 = 0;
    int ret1_2 = 0;
    int ret1_3 = 0;
    int ret2_1 = 0;
    int ret2_2 = 0;
    int ret2_3 = 0;
    static int count = 0;

    ret1_1 = max_read_lock(max_dsi_priv->priv_dsi_client[1], MAX_DP_DESER_SS_B0,
            MAX_DP_DESER_SS_B0_LOCK_MASK,
            MAX_DP_DESER_SS_B0_LOCK_VAL);
    if (ret1_1 < 0) {
        pr_debug("MAX_DSI GMSL1 subsystem statue is not set 0x01\n");
    }

    ret1_2 = max_read_lock(max_dsi_priv->priv_dsi_client[1], MAX_DP_DESER_SS_B1,
            MAX_DP_DESER_SS_B1_LOCK_MASK,
            MAX_DP_DESER_SS_B1_LOCK_VAL);
    if (ret1_2 < 0) {
        pr_debug("MAX_DSI GMSL1 subsystem statue is not set 0x00\n");
    }

    ret1_3 = max_read_lock(max_dsi_priv->priv_dsi_client[1], MAX_DP_DESER_VID,
            MAX_DP_DESER_VID_LOCK_MASK,
            MAX_DP_DESER_VID_LOCK_VAL);
    if (ret1_3 < 0) {
        pr_debug("MAX_DSI GMSL1 video CLk is not set 0x01\n");
    }

    if (max_dsi_priv->split_mode == true) {
        ret2_1 = max_read_lock(max_dsi_priv->priv_dsi_client[2], MAX_DP_DESER_SS_B0,
                MAX_DP_DESER_SS_B0_LOCK_MASK,
                MAX_DP_DESER_SS_B0_LOCK_VAL);
        if (ret2_1 < 0) {
            pr_debug("MAX_DSI GMSL1 subsystem statue is not set 0x01\n");
        }

        ret2_2 = max_read_lock(max_dsi_priv->priv_dsi_client[2], MAX_DP_DESER_SS_B1,
                MAX_DP_DESER_SS_B1_LOCK_MASK,
                MAX_DP_DESER_SS_B1_LOCK_VAL);
        if (ret2_2 < 0) {
            pr_debug("MAX_DSI GMSL1 subsystem statue is not set 0x00\n");
        }

        ret2_3 = max_read_lock(max_dsi_priv->priv_dsi_client[2], MAX_DP_DESER_VID,
                MAX_DP_DESER_VID_LOCK_MASK,
                MAX_DP_DESER_VID_LOCK_VAL);
        if (ret2_3 < 0) {
            pr_debug("MAX_DSI GMSL1 video CLk is not set 0x01\n");
        }

        if (ret1_1 < 0 || ret1_2 < 0 || ret1_3 < 0 ||
            ret2_1 < 0 || ret2_2 < 0 || ret2_3 < 0) {
            pr_debug("MAX_DSI deser rescheule\n");
            goto reschedule;
        }
    }
    else
    {
        if (ret1_1 < 0 || ret1_2 < 0 || ret1_3 < 0 ) {
            pr_debug("MAX_DSI deser rescheule\n");
            goto reschedule;
        }
    }

    pr_debug("MAX_DSI DP deser lock completed, count = %d\n", count);

    return;

reschedule:
    count++;
    queue_delayed_work(max_dsi_priv->deser_wq, &max_dsi_priv->deser_work, msecs_to_jiffies(100));
}

void max_dsi_deser_enable(void)
{
    pr_debug("MAX_DSI [-%s-%s-%d-]\n", __FILE__, __func__, __LINE__);
    // Set Stream1 ID on eDP Deserializer
    max_dsi_ser_write_reg(max_dsi_priv->priv_dsi_client[1], 0x0050, 0x00);
   // Video1 Configuration Registers
   max_dsi_deser_setup(max_dsi_priv->priv_dsi_client[1]);

    // Set Stream2 ID on eDP Deserializer
    max_dsi_ser_write_reg(max_dsi_priv->priv_dsi_client[2], 0x0050, 0x01);
    // Video2 Configuration Registers
    max_dsi_deser_setup(max_dsi_priv->priv_dsi_client[2]);

    queue_delayed_work(max_dsi_priv->deser_wq, &max_dsi_priv->deser_work, msecs_to_jiffies(100));
}

static void max_poll_gmsl_training_lock(struct work_struct *work)
{
    int ret1 = 0;
    int ret2 = 0;
    int ret3 = 0;
    static int count = 0;

    ret1 = max_read_lock(max_dsi_priv->priv_dsi_client[0], MAX_DSI_SER_PCLK,
            MAX_DSI_SER_PCLK_LOCK_MASK,
            MAX_DSI_SER_PCLK_LOCK_VAL);
    if (ret1 < 0) {
        pr_debug("MAX_DSI GMSL PCLk is not set 0x8A\n");
    }

    ret2 = max_read_lock(max_dsi_priv->priv_dsi_client[0], MAX_DSI_SER_ERR,
            MAX_DSI_SER_ERR_LOCK_MASK,
            MAX_DSI_SER_ERR_LOCK_VAL);
    if (ret2 < 0) {
        pr_debug("MAX_DSI GMSL ERR is not set 0x00\n");
    }

    ret3 = max_read_lock(max_dsi_priv->priv_dsi_client[0], MAX_DSI_SER_HS_VS,
            MAX_DSI_SER_HS_VS_LOCK_MASK,
            MAX_DSI_SER_HS_VS_LOCK_VAL);
    if (ret3 < 0) {
        pr_debug("MAX_DSI GMSL HS_VS is not set 0x73\n");
    }

    if (ret1 < 0 || ret2 < 0 || ret3 < 0) {
        pr_debug("MAX_DSI rescheule\n");
        goto reschedule;
    }

    pr_debug("MAX_DSI DP ser training lock completed, count = %d\n", count);

    max_dsi_deser_enable();

    return;

reschedule:
    count++;
    queue_delayed_work(max_dsi_priv->wq, &max_dsi_priv->delay_work, msecs_to_jiffies(500));
}

static int max_dsi_ser_probe(struct i2c_client *client,
        const struct i2c_device_id *idt)
{
    unsigned long type;
    type = idt->driver_data;

    if (max_dsi_priv == NULL){
        max_dsi_priv = devm_kzalloc(&client->dev, sizeof(struct max_dsi_ser_priv),
                       GFP_KERNEL);
        if (max_dsi_priv == NULL)
            return -ENOMEM;

        max_dsi_priv->wq = alloc_workqueue("max_poll_gmsl_training_lock",
                WQ_HIGHPRI, 0);

        INIT_DELAYED_WORK(&max_dsi_priv->delay_work,
                max_poll_gmsl_training_lock);

        max_dsi_priv->deser_wq = alloc_workqueue("max_poll_gmsl_deser_lock",
                WQ_HIGHPRI, 0);

        INIT_DELAYED_WORK(&max_dsi_priv->deser_work,
                max_poll_gmsl_deser_lock);
    }

    if (type == MAX96789) {
        priv_dsi_client[0] = client;
        max_dsi_priv->priv_dsi_client[0] = priv_dsi_client[0];
        pr_debug("MAX_DSI fail [-%s-%s-%d-] MAX96789\n", __FILE__, __func__, __LINE__);
    } else if (type == MAX96772A) {
        priv_dsi_client[1] = client;
        max_dsi_priv->priv_dsi_client[1] = priv_dsi_client[1];
        pr_debug("MAX_DSI fail [-%s-%s-%d-] MAX96772A\n", __FILE__, __func__, __LINE__);
    } else if (type == MAX96772B) {
        priv_dsi_client[2] = client;
        max_dsi_priv->priv_dsi_client[2] = priv_dsi_client[2];
        pr_debug("MAX_DSI fail [-%s-%s-%d-] MAX96772B\n", __FILE__, __func__, __LINE__);
    } else {
        pr_debug("MAX_DSI fail [-%s-%s-%d-]\n", __FILE__, __func__, __LINE__);
    }

    return 0;
}

static int max_dsi_ser_remove(struct i2c_client *client)
{
    pr_debug("MAX_DSI [-%s-%s-%d-]\n", __FILE__, __func__, __LINE__);
    return 0;
}

static const struct i2c_device_id max_dsi_ser_i2c_id_table[] = {
    { "max96789",  MAX96789 },
    { "max96772A", MAX96772A },
    { "max96772B", MAX96772B },
    { },
};

struct i2c_driver max_dsi_ser_drv = {
    .probe = max_dsi_ser_probe,
    .remove= max_dsi_ser_remove,
    .driver= {
    .name = "max96789",
    },
    .id_table = max_dsi_ser_i2c_id_table,
};

static int max_dsi_ser_client_init(void)
{
    int i = 0;
    struct i2c_adapter *i2c_adap;

    i2c_adap = i2c_get_adapter(BUS_DSI_NUMBER);
    if (!i2c_adap) {
        pr_debug("MAX_DSI Cannot find a valid i2c bus for max serdes\n");
        return -ENOMEM;
    }

    for(i = 0; i < NUM_DSI_DEVICE; i++)
        max_dsi_client[i]=i2c_new_client_device(i2c_adap, &max_dsi_i2c_board_info[i]);

    i2c_put_adapter(i2c_adap);

    return 0;
}

static void max_dsi_ser_client_exit(void)
{
    int i = 0;
    for (i = 0; i < NUM_DSI_DEVICE; i++)
        i2c_unregister_device(max_dsi_client[i]);
    pr_debug("MAX_DSI [-%s-%s-%d-]\n", __FILE__, __func__, __LINE__);
}

int intel_dsi_ser_init(void)
{
    max_dsi_ser_enable();
    queue_delayed_work(max_dsi_priv->wq, &max_dsi_priv->delay_work, msecs_to_jiffies(500));

    return 0;
}

void intel_dsi_ser_module_exit(void)
{
    max_dsi_ser_client_exit();
    i2c_del_driver(&max_dsi_ser_drv);
    pr_debug("MAX_DSI [-%s-%s-%d-]\n", __FILE__, __func__, __LINE__);
}

int intel_dsi_ser_module_init(u16 crtc_hdisplay, u16 crtc_vdisplay)
{
    hdisplay = crtc_hdisplay;
    vdisplay = crtc_vdisplay;

    pr_debug("MAX_DSI [-%s-%s-%d-]\n", __FILE__, __func__, __LINE__);
    max_dsi_ser_client_init();
    return i2c_add_driver(&max_dsi_ser_drv);

    return 0;
}

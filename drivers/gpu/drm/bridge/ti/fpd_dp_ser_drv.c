// SPDX-License-Identifier: GPL-2.0-or-later
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

/*
 * Serializer: DS90Ux983-Q1
 * Deserializer 0: DS90Ux984-Q1
 * User Inputs:
 * Deserializer I2C Address = 0x30
 * Deserializer I2C Alias = 0x30
 * Override of DES eFuse enabled
 * FPD-Link III Input Mode
 * DP Port 0 Enabled
 * DP Port 0 PatGen Disabled
 * DP Port 1 Disabled
 * DP Port 1 PatGen Disabled
 * DP Rate set to 2.7 Gbps
 * DP lane number set to 4 lanes
 * FPD3 Video Properties:
 * Total Horizontal Pixels = 2200
 * Total Vertical Lines = 1125
 * Active Horizontal Pixels = 1920
 * Active Vertical Lines = 1080
 * Horizontal Back Porch = 148
 * Vertical Back Porch = 36
 * Horizontal Sync = 44
 * Vertical Sync = 5
 * Horizontal Front Porch = 88
 * Vertical Front Porch = 4
 * Horizontal Sync Polarity = Positive
 * Vertical Sync Polarity = Positive
 * Bits per pixel = 24
 * Pixel Clock = 148.5MHz
 */

#include "fpd_dp_ser_drv.h"

struct i2c_client       *fpd_dp_client[FPD_DP_ARRAY_SIZE];
struct fpd_dp_ser_priv *fpd_dp_priv;

static struct i2c_board_info fpd_dp_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("DS90UB983", FPD_DP_SER_TX_ADD),
	},
	{
		I2C_BOARD_INFO("DS90UB944A", FPD_DP_SER_RX_ADD_A),
	},
};

char fpd_dp_ser_read_reg(struct i2c_client *client, u8 reg_addr, u8 *val)
{
	u8 buf[1];
	int ret = 0;

	struct i2c_msg msg[2];

	buf[0] = reg_addr & 0xff;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &buf[0];
	msg[0].len = 1;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = val;
	msg[1].len = 1;

	i2c_transfer(client->adapter, msg, 2);
	if (ret < 0) {
		pr_debug("[FDP_DP] [-%s-%s-%d-], fail reg_addr=0x%x, val=%u\n",
				__FILE__, __func__, __LINE__, reg_addr, *val);
		return -ENODEV;
	}

	pr_debug("[FDP_DP] 0x%02x, 0x%02x, 0x%02x\n", client->addr, reg_addr, *val);
	return 0;
}

bool fpd_dp_ser_write_reg(struct i2c_client *client, unsigned int reg_addr, u8 val)
{
	int ret = 0;
	struct i2c_msg msg;
	u8 buf[2];

	buf[0] = reg_addr & 0xff;
	buf[1] = val;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.buf = &buf[0];
	msg.len = 2;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret < 0) {
		pr_debug("[FDP_DP] [-%s-%s-%d-], fail client->addr=0x%02x, reg_addr=0x%02x, val=0x%02x\n",
				__FILE__, __func__, __LINE__, client->addr, reg_addr, val);
		return false;
	}
	pr_debug("[FDP_DP] write successful:  0x%02x, 0x%02x, 0x%02x\n",
			client->addr, reg_addr, val);
	return true;
}

/*
 * TODO not used
 * this code is for check i2c return val
 */
static int fpd_dp_read_lock(struct i2c_client *client, unsigned int reg_addr,
		u32 mask, u32 expected_value)
{
	u8 reg_data;

	fpd_dp_ser_read_reg(client, reg_addr, &reg_data);
	if ((reg_data & mask) == expected_value)
		return 0;

	return -1;
}

void fpd_dp_ser_update(struct i2c_client *client,
		u32 reg, u32 mask, u8 val)
{
	u8 update_val;

	fpd_dp_ser_read_reg(client, reg, &update_val);
	update_val = ((update_val & (~mask)) | (val & mask));
	fpd_dp_ser_write_reg(client, reg, update_val);
}

int fpd_dp_ser_prepare(struct i2c_client *client)
{
	u8 TX_MODE_STS;
	u8 GENERAL_CFG;
	u8 read_val;

	pr_debug("[FDP_DP] %s:\n", __func__);

	fpd_dp_ser_write_reg(client, 0x70, FPD_DP_SER_RX_ADD_A);
	fpd_dp_ser_write_reg(client, 0x78, FPD_DP_SER_RX_ADD_A);
	fpd_dp_ser_write_reg(client, 0x88, 0x0);

	/* Check MODE Strapping */
	fpd_dp_ser_read_reg(client, 0x27, &read_val);
	TX_MODE_STS = read_val;

	if (TX_MODE_STS == 0)
		pr_debug("Error: No Serializer Detected\n");

	fpd_dp_ser_read_reg(client, 0x7, &read_val);

	GENERAL_CFG = read_val;
	if ((GENERAL_CFG & 0x01) == 1) {
		pr_debug("MODE Strapped for FPD III Mode\n");
		fpd_dp_priv->FPD4_Strap_Rate_P0 = 0;
		fpd_dp_priv->FPD4_Strap_Rate_P1 = 0;
	} else {
		if ((TX_MODE_STS & 0x0F) == 0x0F) {
			pr_debug("MODE Strapped for FPD III Mode");
			fpd_dp_priv->FPD4_Strap_Rate_P0 = FPD4_Strap_Rate_0;
			fpd_dp_priv->FPD4_Strap_Rate_P1 = FPD4_Strap_Rate_0;
		}
		if (((TX_MODE_STS & 0x0F) == 0x08) || (TX_MODE_STS & 0x0F) == 0x09) {
			pr_debug("MODE Strapped for FPD IV 10.8Gbps");
			fpd_dp_priv->FPD4_Strap_Rate_P0 = FPD4_Strap_Rate_10_8;
			fpd_dp_priv->FPD4_Strap_Rate_P1 = FPD4_Strap_Rate_10_8;
		}
		if (((TX_MODE_STS & 0x0F) == 0x0A || (TX_MODE_STS & 0x0F) == 0x0B)) {
			pr_debug("MODE Strapped for FPD IV 13.5Gbps\n");
			fpd_dp_priv->FPD4_Strap_Rate_P0 = FPD4_Strap_Rate_13_5;
			fpd_dp_priv->FPD4_Strap_Rate_P1 = FPD4_Strap_Rate_13_5;
		}
		if (((TX_MODE_STS & 0x0F) == 0x0C || (TX_MODE_STS & 0x0F) == 0x0D)) {
			pr_debug("MODE Strapped for FPD IV 6.75Gbps\n");
			fpd_dp_priv->FPD4_Strap_Rate_P0 = FPD4_Strap_Rate_6_75;
			fpd_dp_priv->FPD4_Strap_Rate_P1 = FPD4_Strap_Rate_6_75;
		}
		if ((TX_MODE_STS & 0x0F) == 0x0E) {
			pr_debug("MODE Strapped for FPD IV 3.375Gbps\n");
			fpd_dp_priv->FPD4_Strap_Rate_P0 = FPD4_Strap_Rate_3_375;
			fpd_dp_priv->FPD4_Strap_Rate_P1 = FPD4_Strap_Rate_3_375;
		}
	}

	fpd_dp_priv->FPDConf = 8;

	return 0;
}

bool fpd_dp_ser_set_config(struct i2c_client *client)
{
	/* Enable APB Interface */
	fpd_dp_ser_write_reg(client, 0x48, 0x1);

	/* Force HPD low to configure 983 DP settings */
	fpd_dp_ser_write_reg(client, 0x49, 0x0);
	pr_debug("[FDP_DP] Pull HPD low to configure DP settings\n");
	fpd_dp_ser_write_reg(client, 0x4a, 0x0);
	fpd_dp_ser_write_reg(client, 0x4b, 0x0);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	/* Set max advertised link rate = 2.7Gbps */
	fpd_dp_ser_write_reg(client, 0x49, 0x74);
	fpd_dp_ser_write_reg(client, 0x4a, 0x0);
	fpd_dp_ser_write_reg(client, 0x4b, 0xa);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	/* Set max advertised lane count = 4 */
	fpd_dp_ser_write_reg(client, 0x49, 0x70);
	fpd_dp_ser_write_reg(client, 0x4a, 0x0);
	fpd_dp_ser_write_reg(client, 0x4b, 0x4);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	/* Request min VOD swing of 0x02 */
	fpd_dp_ser_write_reg(client, 0x49, 0x14);
	fpd_dp_ser_write_reg(client, 0x4a, 0x2);
	fpd_dp_ser_write_reg(client, 0x4b, 0x2);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	/* Set SST/MST mode and DP/eDP Mode */
	fpd_dp_ser_write_reg(client, 0x49, 0x18);
	fpd_dp_ser_write_reg(client, 0x4a, 0x0);
	fpd_dp_ser_write_reg(client, 0x4b, 0x14);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	/* Force HPD high to trigger link training */
	fpd_dp_ser_write_reg(client, 0x49, 0x0);
	pr_debug("[FDP_DP] Pull HPD High to start link training\n");
	fpd_dp_ser_write_reg(client, 0x4a, 0x0);
	fpd_dp_ser_write_reg(client, 0x4b, 0x1);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	return true;
}

bool fpd_dp_ser_set_port_config(struct i2c_client *client)
{
	u8 read_val;
	u8 GENERAL_CFG;
	u8 GENERAL_CFG_REG;
	u8 FPD3Mask;
	u8 FPD4_CFG;
	u8 TX_MODE_MASK;
	u8 FPD4_CFG_REG;

	fpd_dp_ser_read_reg(client, 0x7, &read_val);
	GENERAL_CFG = read_val;
	FPD3Mask = 0x01;
	GENERAL_CFG_REG = GENERAL_CFG | FPD3Mask;
	/* Set FPD III Mode */
	fpd_dp_ser_write_reg(client, 0x07, GENERAL_CFG_REG);

	fpd_dp_ser_read_reg(client, 0x5, &read_val);
	FPD4_CFG = read_val;
	TX_MODE_MASK = 0xC3;
	FPD4_CFG_REG = FPD4_CFG & TX_MODE_MASK;
	/* Set FPD III Mode */
	fpd_dp_ser_write_reg(client, 0x05, FPD4_CFG_REG);
	/* Set FPD3_TX_MODE to FPD III Independent */
	fpd_dp_ser_write_reg(client, 0x59, 0x5);

	return true;
}

bool fpd_dp_ser_prog_plls(struct i2c_client *client)
{
	/* Set HALFRATE_MODE Override */
	fpd_dp_ser_write_reg(client, 0x2, 0x11);
	/* Set HALFRATE_MODE */
	fpd_dp_ser_write_reg(client, 0x2, 0xd1);
	/* Unset HALFRATE_MODE Override */
	fpd_dp_ser_write_reg(client, 0x2, 0xd0);

	/* Program PLL for Port 0: FPD III Mode 5197.5Mbps */
	fpd_dp_ser_write_reg(client, 0x40, 0x8);
	fpd_dp_ser_write_reg(client, 0x41, 0x4);

	/* Set fractional mash order */
	fpd_dp_ser_write_reg(client, 0x42, 0x9);
	fpd_dp_ser_write_reg(client, 0x41, 0x13);
	/* Set VCO Post Div = 2, VCO Auto Sel for CS2.0 */
	fpd_dp_ser_write_reg(client, 0x42, 0xd0);
	/* Set auto increment */
	fpd_dp_ser_write_reg(client, 0x40, 0xa);
	fpd_dp_ser_write_reg(client, 0x41, 0x5);
	/* Set Ndiv = 96 */
	fpd_dp_ser_write_reg(client, 0x42, 0x60);
	fpd_dp_ser_write_reg(client, 0x42, 0x0);
	fpd_dp_ser_write_reg(client, 0x41, 0x18);
	/* Set denominator = 16777204 */
	fpd_dp_ser_write_reg(client, 0x42, 0xf4);
	fpd_dp_ser_write_reg(client, 0x42, 0xff);
	fpd_dp_ser_write_reg(client, 0x42, 0xff);
	fpd_dp_ser_write_reg(client, 0x41, 0x1e);
	/* Set numerator = 4194301 */
	fpd_dp_ser_write_reg(client, 0x42, 0xfd);
	fpd_dp_ser_write_reg(client, 0x42, 0xff);
	fpd_dp_ser_write_reg(client, 0x42, 0x3f);

	/* Program PLL for Port 1: FPD III Mode 5197.5Mbps */
	fpd_dp_ser_write_reg(client, 0x40, 0x8);
	fpd_dp_ser_write_reg(client, 0x41, 0x44);
	/* Set fractional mash order */
	fpd_dp_ser_write_reg(client, 0x42, 0x9);
	fpd_dp_ser_write_reg(client, 0x41, 0x53);
	/* Set VCO Post Div = 2, VCO Auto Sel for CS2.0 */
	fpd_dp_ser_write_reg(client, 0x42, 0xd0);
	/* Set auto increment */
	fpd_dp_ser_write_reg(client, 0x40, 0xa);
	fpd_dp_ser_write_reg(client, 0x41, 0x45);
	/* Set Ndiv = 96 */
	fpd_dp_ser_write_reg(client, 0x42, 0x60);
	fpd_dp_ser_write_reg(client, 0x42, 0x0);
	fpd_dp_ser_write_reg(client, 0x41, 0x58);
	/* Set denominator = 16777204 */
	fpd_dp_ser_write_reg(client, 0x42, 0xf4);
	fpd_dp_ser_write_reg(client, 0x42, 0xff);
	fpd_dp_ser_write_reg(client, 0x42, 0xff);
	fpd_dp_ser_write_reg(client, 0x41, 0x5e);
	/* Set numerator = 4194301 */
	fpd_dp_ser_write_reg(client, 0x42, 0xfd);
	fpd_dp_ser_write_reg(client, 0x42, 0xff);
	fpd_dp_ser_write_reg(client, 0x42, 0x3f);

	if (fpd_dp_priv->FPD4_Strap_Rate_P0 != FPD4_Strap_Rate_0 ||
			fpd_dp_priv->FPD4_Strap_Rate_P1 != FPD4_Strap_Rate_0) {
		/* Set FPD Page to configure BC Settings for Port 0 and Port 1 */
		fpd_dp_ser_write_reg(client, 0x40, 0x4);
		fpd_dp_ser_write_reg(client, 0x41, 0x6);
		fpd_dp_ser_write_reg(client, 0x42, 0xff);
		fpd_dp_ser_write_reg(client, 0x41, 0xd);
		fpd_dp_ser_write_reg(client, 0x42, 0x70);
		fpd_dp_ser_write_reg(client, 0x41, 0xe);
		fpd_dp_ser_write_reg(client, 0x42, 0x70);
		fpd_dp_ser_write_reg(client, 0x41, 0x26);
		fpd_dp_ser_write_reg(client, 0x42, 0xff);
		fpd_dp_ser_write_reg(client, 0x41, 0x2d);
		fpd_dp_ser_write_reg(client, 0x42, 0x70);
		fpd_dp_ser_write_reg(client, 0x41, 0x2e);
		fpd_dp_ser_write_reg(client, 0x42, 0x70);
	}

	/* Reset PLLs */
	fpd_dp_ser_write_reg(client, 0x1, 0x30);
	/* Wait ~2ms for powerup to complete */
	usleep_range(20000, 22000);

	return true;
}

bool fpd_dp_ser_enable_I2c_passthrough(struct i2c_client *client)
{
	u8 read_val;
	u8 I2C_PASS_THROUGH;
	u8 I2C_PASS_THROUGH_MASK;
	u8 I2C_PASS_THROUGH_REG;

	pr_debug("[FDP_DP] Enable I2C Passthrough\n");

	fpd_dp_ser_read_reg(client, 0x7, &read_val);
	I2C_PASS_THROUGH = read_val;
	I2C_PASS_THROUGH_MASK = 0x08;
	I2C_PASS_THROUGH_REG = I2C_PASS_THROUGH | I2C_PASS_THROUGH_MASK;
	/* Enable I2C Passthrough */
	fpd_dp_ser_write_reg(client, 0x07, I2C_PASS_THROUGH_REG);

	return true;
}

bool fpd_dp_ser_prog_vp_configs(struct i2c_client *client)
{
	pr_debug("[FDP_DP] Configure Video Processors\n");
	/* Configure VP 0 */
	fpd_dp_ser_write_reg(client, 0x40, 0x32);
	fpd_dp_ser_write_reg(client, 0x41, 0x1);
	/* Set VP_SRC_SELECT to Stream 0 for SST Mode */
	fpd_dp_ser_write_reg(client, 0x42, 0xa8);
	fpd_dp_ser_write_reg(client, 0x41, 0x2);
	/* VID H Active */
	fpd_dp_ser_write_reg(client, 0x42, 0x80);
	fpd_dp_ser_write_reg(client, 0x42, 0x7);
	fpd_dp_ser_write_reg(client, 0x41, 0x10);
	/* Horizontal Active */
	fpd_dp_ser_write_reg(client, 0x42, 0x80);
	fpd_dp_ser_write_reg(client, 0x42, 0x7);
	/* Horizontal Back Porch */
	fpd_dp_ser_write_reg(client, 0x42, 0x94);
	fpd_dp_ser_write_reg(client, 0x42, 0x0);
	/* Horizontal Sync */
	fpd_dp_ser_write_reg(client, 0x42, 0x2c);
	fpd_dp_ser_write_reg(client, 0x42, 0x0);
	/* Horizontal Total */
	fpd_dp_ser_write_reg(client, 0x42, 0x98);
	fpd_dp_ser_write_reg(client, 0x42, 0x8);
	/* Vertical Active */
	fpd_dp_ser_write_reg(client, 0x42, 0x38);
	fpd_dp_ser_write_reg(client, 0x42, 0x4);
	/* Vertical Back Porch */
	fpd_dp_ser_write_reg(client, 0x42, 0x24);
	fpd_dp_ser_write_reg(client, 0x42, 0x0);
	/* Vertical Sync */
	fpd_dp_ser_write_reg(client, 0x42, 0x5);
	fpd_dp_ser_write_reg(client, 0x42, 0x0);
	/* Vertical Front Porch */
	fpd_dp_ser_write_reg(client, 0x42, 0x4);
	fpd_dp_ser_write_reg(client, 0x42, 0x0);
	fpd_dp_ser_write_reg(client, 0x41, 0x27);
	/* HSYNC Polarity = +, VSYNC Polarity = + */
	fpd_dp_ser_write_reg(client, 0x42, 0x0);

	/* Configure VP 1 */
	fpd_dp_ser_write_reg(client, 0x40, 0x32);
	fpd_dp_ser_write_reg(client, 0x41, 0x41);
	/* Set VP_SRC_SELECT to Stream 0 for SST Mode */
	fpd_dp_ser_write_reg(client, 0x42, 0xa8);
	fpd_dp_ser_write_reg(client, 0x41, 0x42);
	/* VID H Active */
	fpd_dp_ser_write_reg(client, 0x42, 0x80);
	fpd_dp_ser_write_reg(client, 0x42, 0x7);
	fpd_dp_ser_write_reg(client, 0x41, 0x50);
	/* Horizontal Active */
	fpd_dp_ser_write_reg(client, 0x42, 0x80);
	fpd_dp_ser_write_reg(client, 0x42, 0x7);
	/* Horizontal Back Porch */
	fpd_dp_ser_write_reg(client, 0x42, 0x94);
	fpd_dp_ser_write_reg(client, 0x42, 0x0);
	/* Horizontal Sync */
	fpd_dp_ser_write_reg(client, 0x42, 0x2c);
	fpd_dp_ser_write_reg(client, 0x42, 0x0);
	/* Horizontal Total */
	fpd_dp_ser_write_reg(client, 0x42, 0x98);
	fpd_dp_ser_write_reg(client, 0x42, 0x8);
	/* Vertical Active */
	fpd_dp_ser_write_reg(client, 0x42, 0x38);
	fpd_dp_ser_write_reg(client, 0x42, 0x4);
	/* Vertical Back Porch */
	fpd_dp_ser_write_reg(client, 0x42, 0x24);
	fpd_dp_ser_write_reg(client, 0x42, 0x0);
	/* Vertical Sync */
	fpd_dp_ser_write_reg(client, 0x42, 0x5);
	fpd_dp_ser_write_reg(client, 0x42, 0x0);
	/* Vertical Front Porch */
	fpd_dp_ser_write_reg(client, 0x42, 0x4);
	fpd_dp_ser_write_reg(client, 0x42, 0x0);
	fpd_dp_ser_write_reg(client, 0x41, 0x67);
	/* HSYNC Polarity = +, VSYNC Polarity = + */
	fpd_dp_ser_write_reg(client, 0x42, 0x0);

	return true;
}

/**
 * Enable VPs
 */
bool fpd_dp_ser_enable_vps(struct i2c_client *client)
{
	pr_debug("[FDP_DP] Enable Video Processors\n");
	/* Set number of VPs used = 2 */
	fpd_dp_ser_write_reg(client, 0x43, 0x1);
	/* Enable video processors */
	fpd_dp_ser_write_reg(client, 0x44, 0x3);

	return true;
}

/**
 * Set FPD3 Stream Mapping
 */
bool fpd_dp_ser_stream_mapping(struct i2c_client *client)
{
	pr_debug("[FDP_DP] Set FPD3 Stream Mappingn\n");
	/* Select FPD TX Port 0 */
	fpd_dp_ser_write_reg(client, 0x2d, 0x1);
	/* Set FPD TX Port 0 Stream Source = VP1 */
	fpd_dp_ser_write_reg(client, 0x57, 0x1);
	/* Select FPD TX Port 1 */
	fpd_dp_ser_write_reg(client, 0x2d, 0x12);
	/* Set FPD TX Port 1 Stream Source = VP0 */
	fpd_dp_ser_write_reg(client, 0x57, 0x0);
	/* Enable FPD III FIFO */
	fpd_dp_ser_write_reg(client, 0x5b, 0x2b);

	return true;
}

bool fpd_dp_ser_clear_crc(struct i2c_client *client)
{
	u8 Reg_value;

	pr_debug("[FDP_DP] Clear CRC errors from initial link process\n");

	fpd_dp_ser_read_reg(client, 0x2, &Reg_value);
	Reg_value = Reg_value | 0x20;
	/* CRC Error Reset */
	fpd_dp_ser_write_reg(client, 0x2, Reg_value);

	fpd_dp_ser_read_reg(client, 0x2, &Reg_value);
	Reg_value = Reg_value & 0xdf;
	/* CRC Error Reset Clear */
	fpd_dp_ser_write_reg(client, 0x2, Reg_value);

	fpd_dp_ser_write_reg(client, 0x2d, 0x1);
	usleep_range(20000, 22000);

	return true;
}

bool fpd_dp_ser_setup(struct i2c_client *client)
{
	fpd_dp_ser_set_config(client);
	fpd_dp_ser_set_port_config(client);
	fpd_dp_ser_prog_plls(client);
	fpd_dp_ser_enable_I2c_passthrough(client);
	fpd_dp_ser_prog_vp_configs(client);
	fpd_dp_ser_enable_vps(client);
	/* Check if VP is synchronized to DP input */
	queue_delayed_work(fpd_dp_priv->wq, &fpd_dp_priv->delay_work, msecs_to_jiffies(100));

	return true;
}

bool fpd_dp_ser_enable(void)
{
	fpd_dp_ser_prepare(fpd_dp_priv->priv_dp_client[0]);
	if (false == fpd_dp_ser_setup(fpd_dp_priv->priv_dp_client[0])) {
		pr_debug("[FDP_DP] DS90UB983 enable fail\n");
		return false;
	}
	return true;
}

void fpd_dp_deser_override_efuse(struct i2c_client *client)
{
	u8 DES_READBACK;

	fpd_dp_ser_read_reg(client, 0x0, &DES_READBACK);
	if (DES_READBACK == 0)
		pr_debug("[FDP_DP] Error - no DES detected\n");
	else
		pr_debug("[FDP_DP] Deserializer detected successfully\n");

	fpd_dp_ser_write_reg(client, 0x49, 0xc);
	fpd_dp_ser_write_reg(client, 0x4a, 0x0);
	fpd_dp_ser_write_reg(client, 0x48, 0x1b);
	usleep_range(20000, 22000);
}

void fpd_dp_deser_hold_dtg_reset(struct i2c_client *client)
{
	pr_debug("[FDP_DP] Hold Des 0 DTG in reset and configure video settings\n");
	/* Select DTG Page */
	fpd_dp_ser_write_reg(client, 0x40, 0x50);
	fpd_dp_ser_write_reg(client, 0x41, 0x32);
	/* Hold Local Display Output Port 0 DTG in Reset */
	fpd_dp_ser_write_reg(client, 0x42, 0x6);
	fpd_dp_ser_write_reg(client, 0x41, 0x62);
	/* Hold Local Display Output Port 1 DTG in Reset */
	fpd_dp_ser_write_reg(client, 0x42, 0x6);
}

void fpd_dp_deser_disalbe_stream_mapping(struct i2c_client *client)
{
	pr_debug("[FDP_DP] Hold Des 0 DTG in reset and configure video settings\n");
	/* Select both Output Ports */
	fpd_dp_ser_write_reg(client, 0xe, 0x3);
	/* Disable FPD4 video forward to Output Port */
	fpd_dp_ser_write_reg(client, 0xd0, 0x0);
	/* Disable FPD3 video forward to Output Port */
	fpd_dp_ser_write_reg(client, 0xd7, 0x0);
}

void fpd_dp_deser_force_rate(struct i2c_client *client)
{
	pr_debug("[FDP_DP] Hold Des 0 DTG in reset and configure video settings\n");
	/* Select DP Page */
	fpd_dp_ser_write_reg(client, 0x40, 0x2c);
	fpd_dp_ser_write_reg(client, 0x41, 0x81);
	/* Set DP Rate to 2.7Gbps */
	fpd_dp_ser_write_reg(client, 0x42, 0x60);
	fpd_dp_ser_write_reg(client, 0x41, 0x82);
	/* Enable force DP rate with calibration disabled */
	fpd_dp_ser_write_reg(client, 0x42, 0x3);
	/* Select DP Page */
	fpd_dp_ser_write_reg(client, 0x40, 0x2c);
	fpd_dp_ser_write_reg(client, 0x41, 0x91);
	/* Force 4 lanes */
	fpd_dp_ser_write_reg(client, 0x42, 0xc);
	/* Disable DP SSCG */
	fpd_dp_ser_write_reg(client, 0x40, 0x30);
	fpd_dp_ser_write_reg(client, 0x41, 0xf);
	fpd_dp_ser_write_reg(client, 0x42, 0x1);
	fpd_dp_ser_write_reg(client, 0x1, 0x40);
}

void fpd_dp_deser_setup_ports(struct i2c_client *client)
{
	pr_debug("[FDP_DP] Hold Des 0 DTG in reset and configure video settings\n");
	/* Select Port 1 registers */
	fpd_dp_ser_write_reg(client, 0xe, 0x12);
	/* Disable DP Port 1 */
	fpd_dp_ser_write_reg(client, 0x46, 0x0);
	/* Select Port 0 registers */
	fpd_dp_ser_write_reg(client, 0xe, 0x1);
	/* DP-TX-PLL RESET Applied */
	fpd_dp_ser_write_reg(client, 0x1, 0x40);
}

void fpd_dp_deser_map_output(struct i2c_client *client)
{
	pr_debug("[FDP_DP] Hold Des 0 DTG in reset and configure video settings\n");
	/* Select both Output Ports */
	fpd_dp_ser_write_reg(client, 0xe, 0x3);
	/* Disable FPD4 video forward to local display output */
	fpd_dp_ser_write_reg(client, 0xd0, 0x0);
	/* Disable stream forwarding on DC */
	fpd_dp_ser_write_reg(client, 0xd1, 0x0);
	fpd_dp_ser_write_reg(client, 0xd6, 0x0);
	/* Enable FPD3 to local display output mapping */
	fpd_dp_ser_write_reg(client, 0xd7, 0xc);
	/* Select Port 0 */
	fpd_dp_ser_write_reg(client, 0xe, 0x1);
}

void fpd_dp_deser_prog_pclk(struct i2c_client *client)
{
	pr_debug("[FDP_DP] Hold Des 0 DTG in reset and configure video settings\n");
	/* Select Port0 registers */
	fpd_dp_ser_write_reg(client, 0xe, 0x1);
	/* Enable clock divider */
	fpd_dp_ser_write_reg(client, 0xb1, 0x1);
	/* Enable clock divider */
	fpd_dp_ser_write_reg(client, 0xb2, 0x14);
	/* Program M value middle byte */
	fpd_dp_ser_write_reg(client, 0xb3, 0x44);
	/* Program M value middle byte */
	fpd_dp_ser_write_reg(client, 0xb4, 0x2);
	/* Program N value lower byte */
	fpd_dp_ser_write_reg(client, 0xb5, 0xc0);
	/* Program N value middle byte */
	fpd_dp_ser_write_reg(client, 0xb6, 0x7a);
	/* Program N value upper byte */
	fpd_dp_ser_write_reg(client, 0xb7, 0x10);
	/* Select Port 0 registers */
	fpd_dp_ser_write_reg(client, 0xe, 0x1);
}

void fpd_dp_deser_setup_dtg(struct i2c_client *client)
{
	pr_debug("[FDP_DP] Hold Des 0 DTG in reset and configure video settings\n");
	/* Select DTG Page */
	fpd_dp_ser_write_reg(client, 0x40, 0x50);
	fpd_dp_ser_write_reg(client, 0x41, 0x20);
	/* Set up Local Display DTG BPP, Sync Polarities, and Measurement Type */
	fpd_dp_ser_write_reg(client, 0x42, 0x93);
	/* Set Hstart */
	fpd_dp_ser_write_reg(client, 0x41, 0x29);
	/* Hstart upper byte */
	fpd_dp_ser_write_reg(client, 0x42, 0x80);
	fpd_dp_ser_write_reg(client, 0x41, 0x2a);
	/* Hstart lower byte */
	fpd_dp_ser_write_reg(client, 0x42, 0xc0);
	/* Set HSW */
	fpd_dp_ser_write_reg(client, 0x41, 0x2f);
	/* HSW upper byte */
	fpd_dp_ser_write_reg(client, 0x42, 0x40);
	fpd_dp_ser_write_reg(client, 0x41, 0x30);
	/* HSW lower byte */
	fpd_dp_ser_write_reg(client, 0x42, 0x2c);
}

void fpd_dp_deser_setup_dptx(struct i2c_client *client)
{
	pr_debug("[FDP_DP] Hold Des 0 DTG in reset and configure video settings\n");
	/* Enable APB interface */
	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	/* Set bit per color */
	fpd_dp_ser_write_reg(client, 0x49, 0xa4);
	fpd_dp_ser_write_reg(client, 0x4a, 0x1);
	fpd_dp_ser_write_reg(client, 0x4b, 0x20);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	/* Set pixel width */
	fpd_dp_ser_write_reg(client, 0x49, 0xb8);
	fpd_dp_ser_write_reg(client, 0x4a, 0x1);
	fpd_dp_ser_write_reg(client, 0x4b, 0x4);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	/* Set DP Mvid */
	fpd_dp_ser_write_reg(client, 0x49, 0xac);
	fpd_dp_ser_write_reg(client, 0x4a, 0x1);
	fpd_dp_ser_write_reg(client, 0x4b, 0x66);
	fpd_dp_ser_write_reg(client, 0x4c, 0x46);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	/* Set DP Nvid */
	fpd_dp_ser_write_reg(client, 0x49, 0xb4);
	fpd_dp_ser_write_reg(client, 0x4a, 0x1);
	fpd_dp_ser_write_reg(client, 0x4b, 0x0);
	fpd_dp_ser_write_reg(client, 0x4c, 0x80);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	/* Set TU Mode */
	fpd_dp_ser_write_reg(client, 0x49, 0xc8);
	fpd_dp_ser_write_reg(client, 0x4a, 0x1);
	fpd_dp_ser_write_reg(client, 0x4b, 0x0);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	/* Set TU Size */
	fpd_dp_ser_write_reg(client, 0x49, 0xb0);
	fpd_dp_ser_write_reg(client, 0x4a, 0x1);
	fpd_dp_ser_write_reg(client, 0x4b, 0x40);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x1a);
	fpd_dp_ser_write_reg(client, 0x4e, 0x8);

	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	/* Set FIFO Size */
	fpd_dp_ser_write_reg(client, 0x49, 0xc8);
	fpd_dp_ser_write_reg(client, 0x4a, 0x0);
	fpd_dp_ser_write_reg(client, 0x4b, 0x6);
	fpd_dp_ser_write_reg(client, 0x4c, 0x40);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	/* Set data count */
	fpd_dp_ser_write_reg(client, 0x49, 0xbc);
	fpd_dp_ser_write_reg(client, 0x4a, 0x1);
	fpd_dp_ser_write_reg(client, 0x4b, 0xa0);
	fpd_dp_ser_write_reg(client, 0x4c, 0x5);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	/* Disable STREAM INTERLACED */
	fpd_dp_ser_write_reg(client, 0x49, 0xc0);
	fpd_dp_ser_write_reg(client, 0x4a, 0x1);
	fpd_dp_ser_write_reg(client, 0x4b, 0x0);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	/* Set SYNC polarity */
	fpd_dp_ser_write_reg(client, 0x49, 0xc4);
	fpd_dp_ser_write_reg(client, 0x4a, 0x1);
	fpd_dp_ser_write_reg(client, 0x4b, 0xc);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);
}

void fpd_dp_deser_release_dtg_reset(struct i2c_client *client)
{
	pr_debug("[FDP_DP] Release Des 0 DTG reset and enable video output\n");
	/* Select DTG Page */
	fpd_dp_ser_write_reg(client, 0x40, 0x50);
	fpd_dp_ser_write_reg(client, 0x41, 0x32);
	/* Select DTG Page */
	fpd_dp_ser_write_reg(client, 0x42, 0x4);
	fpd_dp_ser_write_reg(client, 0x41, 0x62);
	/* Release Local Display Output Port 1 DTG */
	fpd_dp_ser_write_reg(client, 0x42, 0x4);

	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	/* Set Htotal */
	fpd_dp_ser_write_reg(client, 0x49, 0x80);
	fpd_dp_ser_write_reg(client, 0x4a, 0x1);
	fpd_dp_ser_write_reg(client, 0x4b, 0x98);
	fpd_dp_ser_write_reg(client, 0x4c, 0x8);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);

}

void fpd_dp_deser_enable_output(struct i2c_client *client)
{
	fpd_dp_ser_write_reg(client, 0x48, 0x1);
	/* Enable DP output */
	fpd_dp_ser_write_reg(client, 0x49, 0x84);
	fpd_dp_ser_write_reg(client, 0x4a, 0x0);
	fpd_dp_ser_write_reg(client, 0x4b, 0x1);
	fpd_dp_ser_write_reg(client, 0x4c, 0x0);
	fpd_dp_ser_write_reg(client, 0x4d, 0x0);
	fpd_dp_ser_write_reg(client, 0x4e, 0x0);
}

void fpd_dp_deser_enable(void)
{
	pr_debug("[FDP_DP] [-%s-%s-%d-]\n", __FILE__, __func__, __LINE__);
	/* Enable I2C Passthrough - is to communicate locally with the DES */
	fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x3, 0x9a);
	fpd_dp_deser_override_efuse(fpd_dp_priv->priv_dp_client[1]);
	fpd_dp_deser_hold_dtg_reset(fpd_dp_priv->priv_dp_client[1]);
	fpd_dp_deser_disalbe_stream_mapping(fpd_dp_priv->priv_dp_client[1]);
	fpd_dp_deser_force_rate(fpd_dp_priv->priv_dp_client[1]);
	fpd_dp_deser_setup_ports(fpd_dp_priv->priv_dp_client[1]);
	fpd_dp_deser_map_output(fpd_dp_priv->priv_dp_client[1]);
	fpd_dp_deser_prog_pclk(fpd_dp_priv->priv_dp_client[1]);
	fpd_dp_deser_setup_dtg(fpd_dp_priv->priv_dp_client[1]);
	fpd_dp_deser_setup_dptx(fpd_dp_priv->priv_dp_client[1]);
	fpd_dp_deser_release_dtg_reset(fpd_dp_priv->priv_dp_client[1]);
	fpd_dp_deser_enable_output(fpd_dp_priv->priv_dp_client[1]);
}

#define RETRY_COUNT 10

static void fpd_poll_training_lock(struct work_struct *work)
{
	u8 PATGEN_VP0 = 0;
	u8 VP0sts = 0;
	u8 PATGEN_VP1 = 0;
	u8 VP1sts = 0;
	int retry = 0;

	pr_debug("[FDP_DP] Check if VP is synchronized to DP input\n");

	/* Delay for VPs to sync to DP source */
	usleep_range(20000, 22000);

	/* Select VP Page */
	fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x40, 0x31);
	fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x41, 0x28);
	fpd_dp_ser_read_reg(fpd_dp_priv->priv_dp_client[0], 0x42, &PATGEN_VP0);
	fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x41, 0x30);
	fpd_dp_ser_read_reg(fpd_dp_priv->priv_dp_client[0], 0x42, &VP0sts);
	pr_debug("[FDP_DP] VP0sts = 0x%02x\n", (VP0sts & 0x01));

	while (((VP0sts & 0x01) == 0) && retry < RETRY_COUNT && ((PATGEN_VP0 & 0x01) == 0)) {
		pr_debug("[FDP_DP] VP0 Not Synced - Delaying 100ms. Retry = %d\n", retry);
		usleep_range(20000, 22000);
		fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x41, 0x30);
		fpd_dp_ser_read_reg(fpd_dp_priv->priv_dp_client[0], 0x42, &VP0sts);
		retry = retry + 1;
	}

	if (((VP0sts & 0x01) == 1) && retry < RETRY_COUNT && ((PATGEN_VP0 & 0x01) == 0))
		pr_debug("[FDP_DP] VP0 Syned\n");
	else
		pr_debug("[FDP_DP] Unable to achieve VP0 sync\n");
	if ((PATGEN_VP0 & 0x01) == 1)
		pr_debug("[FDP_DP] VP0 sync status bypassed since PATGEN is enabled\n");

	retry = 0;
	fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x41, 0x68);
	fpd_dp_ser_read_reg(fpd_dp_priv->priv_dp_client[0], 0x42, &PATGEN_VP1);
	fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x41, 0x70);
	fpd_dp_ser_read_reg(fpd_dp_priv->priv_dp_client[0], 0x42, &VP1sts);
	pr_debug("[FDP_DP] VP1sts = 0x%02x\n", (VP1sts & 0x01));

	while (((VP1sts & 0x01) == 0) && retry < RETRY_COUNT && ((PATGEN_VP1 & 0x01) == 0)) {
		pr_debug("[FDP_DP]  VP1 Not Synced - Delaying 100ms. Retry =%d\n", retry);
		usleep_range(20000, 22000);
		fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x41, 0x70);
		fpd_dp_ser_read_reg(fpd_dp_priv->priv_dp_client[0], 0x42, &VP1sts);
		retry = retry + 1;
	}

	if (((VP1sts & 0x01) == 1) && retry < RETRY_COUNT && ((PATGEN_VP1 & 0x01) == 0))
		pr_debug("[FDP_DP]  VP1 Syned\n");
	else
		pr_debug("[FDP_DP]  Unable to achieve VP1 sync\n");
	if ((PATGEN_VP1 & 0x01) == 1)
		pr_debug("[FDP_DP]  VP1 sync status bypassed since PATGEN is enabled\n");

	if (((VP0sts & 0x01) == 0) || ((VP1sts & 0x01) == 0)) {
		pr_debug("[FDP_DP]  VPs not synchronized - performing video input reset\n");
		/* Video Input Reset if VP is not synchronized */
		fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x49, 0x54);
		fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x4a, 0x0);
		fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x4b, 0x1);
		fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x4c, 0x0);
		fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x4d, 0x0);
		fpd_dp_ser_write_reg(fpd_dp_priv->priv_dp_client[0], 0x4e, 0x0);
		goto reschedule;
	}

	pr_debug("[FDP_DP] ser training lock completed, count = %d\n", fpd_dp_priv->count);
	fpd_dp_priv->count = 0;
	fpd_dp_ser_stream_mapping(fpd_dp_priv->priv_dp_client[0]);

	fpd_dp_ser_clear_crc(fpd_dp_priv->priv_dp_client[0]);

	fpd_dp_deser_enable();

	return;

reschedule:
	fpd_dp_priv->count++;
	if (fpd_dp_priv->count > RETRY_COUNT) {
		pr_debug("[FDP_DP] ser training lock failed, count = %d\n", fpd_dp_priv->count);
		return;
	}

	queue_delayed_work(fpd_dp_priv->wq, &fpd_dp_priv->delay_work, msecs_to_jiffies(100));
}

static int fpd_dp_ser_probe(struct i2c_client *client,
		const struct i2c_device_id *idt)
{
	unsigned long type;

	type = idt->driver_data;

	if (fpd_dp_priv == NULL) {
		fpd_dp_priv = devm_kzalloc(&client->dev, sizeof(struct fpd_dp_ser_priv),
				GFP_KERNEL);
		if (fpd_dp_priv == NULL)
			return -ENOMEM;

		fpd_dp_priv->wq = alloc_workqueue("fpd_poll_training_lock",
				WQ_HIGHPRI, 0);

		if (unlikely(!fpd_dp_priv->wq)) {
			pr_debug("[FDP_DP] Failed to allocate workqueue\n");
			return -ENOMEM;
		}

		INIT_DELAYED_WORK(&fpd_dp_priv->delay_work,
				fpd_poll_training_lock);

		fpd_dp_priv->count = 0;
	}

	if (type == DS90UB983) {
		fpd_dp_priv->priv_dp_client[0] = client;
		pr_debug("[FDP_DP] [-%s-%s-%d-] DS90UB983\n", __FILE__, __func__, __LINE__);
		fpd_dp_ser_init();
	} else if (type == DS90UB944A) {
		fpd_dp_priv->priv_dp_client[1] = client;
		pr_debug("[FDP_DP] [-%s-%s-%d-] DS90UB984A\n", __FILE__, __func__, __LINE__);
	} else {
		pr_debug("[FDP_DP] fail [-%s-%s-%d-]\n", __FILE__, __func__, __LINE__);
	}

	return 0;
}

static int fpd_dp_ser_remove(struct i2c_client *client)
{
	pr_debug("[FDP_DP] [-%s-%s-%d-]\n", __FILE__, __func__, __LINE__);
	return 0;
}

static const struct i2c_device_id fpd_dp_ser_i2c_id_table[] = {
	{ "DS90UB983",  DS90UB983 },
	{ "DS90UB944A", DS90UB944A },
	{ },
};

struct i2c_driver fpd_dp_ser_drv = {
	.probe = fpd_dp_ser_probe,
	.remove = fpd_dp_ser_remove,
	.driver = {
		.name = "DS90UB983",
	},
	.id_table = fpd_dp_ser_i2c_id_table,
};

static int intel_get_i2c_bus_id(int adapter_id, char *adapter_bdf, int bdf_len)
{
	struct i2c_adapter *adapter;
	struct device *parent;
	struct device *pp;
	int i = 0;
	int found = 0;
	int retry_count = 0;

	if (!adapter_bdf || bdf_len > 32)
		return -1;

	while (retry_count < 5) {
		i = 0;
		found = 0;
		while ((adapter = i2c_get_adapter(i)) != NULL) {
			parent = adapter->dev.parent;
			pp = parent->parent;
			i2c_put_adapter(adapter);
			pr_debug("[FDP_DP] dev_name(pp): %s\n", dev_name(pp));
			if (pp && !strncmp(adapter_bdf, dev_name(pp), bdf_len)) {
				found = 1;
				break;
			}
			i++;
		}

		if (found) {
			pr_debug("[FDP_DP] found dev_name(pp) %s\n", dev_name(pp));
			break;
		}
		retry_count++;
		pr_debug("[FDP_DP] not found retry_count %d\n", retry_count);
		msleep(1000);
	}

	if (found)
		return i;

	/* Not found */
	return -1;
}


static int get_bus_number(void)
{
	char adapter_bdf[32] = ADAPTER_PP_DEV_NAME;
	int bus_number = intel_get_i2c_bus_id(0, adapter_bdf, 32);
	return bus_number;
}

static int fpd_dp_ser_client_init(void)
{
	int i = 0;
	struct i2c_adapter *i2c_adap;

	int bus_num = get_bus_number();

	if (bus_num < 0) {
		pr_debug("[FDP_DP] Cannot find a valid i2c bus for serdes\n");
		return -ENOMEM;
	}

	i2c_adap = i2c_get_adapter(bus_num);
	if (!i2c_adap) {
		pr_debug("[FDP_DP] Cannot find a valid i2c bus for serdes\n");
		return -ENOMEM;
	}

	for (i = 0; i < NUM_DP_DEVICE; i++) {
		fpd_dp_client[i] = i2c_new_client_device(i2c_adap, &fpd_dp_i2c_board_info[i]);
		if (fpd_dp_client[i] == NULL) {
			pr_debug("[FDP_DP] Cannot create i2c client device\n");
			return -ENOMEM;
		}
	}

	i2c_put_adapter(i2c_adap);

	return 0;
}

static void fpd_dp_ser_client_exit(void)
{
	int i = 0;

	for (i = 0; i < NUM_DP_DEVICE; i++)
		i2c_unregister_device(fpd_dp_client[i]);
	pr_debug("[FDP_DP] [-%s-%s-%d-]\n", __FILE__, __func__, __LINE__);
}

int fpd_dp_ser_init(void)
{
	fpd_dp_ser_enable();

	return 0;
}

int fpd_dp_ser_module_init(void)
{
	pr_debug("[FDP_DP] [-%s-%s-%d-]\n", __FILE__, __func__, __LINE__);
	fpd_dp_ser_client_init();
	return i2c_add_driver(&fpd_dp_ser_drv);

	return 0;
}

void fpd_dp_ser_module_exit(void)
{
	fpd_dp_ser_client_exit();
	i2c_del_driver(&fpd_dp_ser_drv);
	pr_debug("[FDP_DP] [-%s-%s-%d-]\n", __FILE__, __func__, __LINE__);
}

#ifdef MODULE
module_init(fpd_dp_ser_module_init);
module_exit(fpd_dp_ser_module_exit);
#else
late_initcall(fpd_dp_ser_module_init);
#endif

MODULE_DESCRIPTION("TI serdes 983 984 driver");
MODULE_AUTHOR("Jia, Lin A <lin.a.jia@intel.com>");
MODULE_AUTHOR("Hu, Kanli <kanli.hu@intel.com>");
MODULE_LICENSE("GPL v2");

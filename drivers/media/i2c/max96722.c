// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 Intel Corporation.

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fwnode.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/ipu-isys.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include <media/max96722.h>


#define to_max96722(_sd) container_of(_sd, struct max96722_priv, sd)

#define MAX96722_NUM_GMSL 4
#define MAX96722_N_SINKS 4
#define MAX96722_N_PADS 5
#define MAX96722_SRC_PAD 4

#define DELAY_MS 100

struct max96722_reg {
	u16 address;
	u8 val;
};

struct max96722_reg_list {
	u32 num_of_regs;
	const struct max96722_reg *regs;
};

static struct regmap_config config16 = {
	.reg_bits = 16,
	.val_bits = 8,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
};

static s64 max96722_query_sub_stream[] = {
	0, 0, 0, 0
};

#define MIPI_CSI2_TYPE_YUV422_8 0x1e
static unsigned int mbus_code_to_mipi(u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_UYVY8_1X16:
		return MIPI_CSI2_TYPE_YUV422_8;
	default:
		WARN_ON(1);
		return -EINVAL;
	}
}

/*
 * FSYNC_MODE 2b01 FSYNC_METH 2b00
 * 25Mhz XTAL, 30fps, TX_ID 8
 * OVLP window 0
 * enable fsync on pipe 1
 */
static const struct max96722_reg fsync_30fps[] = {
	{0x04a0, 0x04},
	{0x04a2, 0x00},
	{0x04aa, 0x00},
	{0x04ab, 0x00},
	{0x04af, 0xc2},
	{0x04a7, 0x0c},
	{0x04a6, 0xb7},
	{0x04a5, 0x35},
	{0x04b1, 0x40},
};
static const struct max96722_reg_list fsync_setting = {
	.num_of_regs = ARRAY_SIZE(fsync_30fps),
	.regs = fsync_30fps,
};

/*
 * disable CSI out
 *
 * 2x4 800MBps 4lanes
 * lanes swapped matches pin
 * enable PHY 0/1/2/3
 * write to 0x40b to enable csi out
 */
static const struct max96722_reg csi_phy[] = {
	{0x040b, 0x00},
	{0x08a0, 0x04},
	{0x08a3, 0xe4},
	{0x094a, 0xc0},
	{0x1d00, 0xf4},
	{0x0418, 0x28},
	{0x1d00, 0xf5},
	{0x08a2, 0xf0},
};
static const struct max96722_reg_list mipi_phy_setting = {
	.num_of_regs = ARRAY_SIZE(csi_phy),
	.regs = csi_phy,
};

/*
 * link a pipe z -> pipe 0
 * link b pipe x -> pipe 1
 * enable pipe 0/1/2/3
 */
static const struct max96722_reg video_pipe_sel[] = {
	{0x00f0, 0x42},
	{0x00f4, 0x0f},
};
static const struct max96722_reg_list video_pipe_setting = {
	.num_of_regs = ARRAY_SIZE(video_pipe_sel),
	.regs = video_pipe_sel,
};

/*
 * pipe 0 (MIPI CSI) use value from ser
 *
 * pipe 1 (DVP) software overwrite
 * vc - 0, dt - 0x1e, bpp - 8
 * muxed mode enable
 */
static const struct max96722_reg video_pipe_conf[] = {
	{0x0415, 0x80},
	{0x040e, 0x40},
	{0x040f, 0x0e},
	{0x0411, 0x08},
	{0x041a, 0x20},
};
static const struct max96722_reg_list backtop_setting = {
	.num_of_regs = ARRAY_SIZE(video_pipe_conf),
	.regs = video_pipe_conf,
};

/*
 * pipe 0 vc0
 * FS/DATA/FE identity mapping
 * to csi ctrl 1
 * pipe 1 vc1
 * FS/DATA/FE identity mapping
 * to csi ctrl 1
 */
static const struct max96722_reg video_pipe_to_csi_ctrl_mapping[] = {
	{0x090b, 0x07},
	{0x090d, 0x00},
	{0x090e, 0x00},
	{0x090f, 0x1e},
	{0x0910, 0x1e},
	{0x0911, 0x01},
	{0x0912, 0x01},
	{0x092d, 0x15},
	/* pipe 1 */
	{0x094b, 0x07},
	{0x094d, 0x00},
	{0x094e, 0x40},
	{0x094f, 0x1e},
	{0x0950, 0x5e},
	{0x0951, 0x01},
	{0x0952, 0x41},
	{0x096d, 0x15},
};

static const struct max96722_reg_list mipi_ctrl_setting = {
	.num_of_regs = ARRAY_SIZE(video_pipe_to_csi_ctrl_mapping),
	.regs = video_pipe_to_csi_ctrl_mapping,
};

static const struct max96722_reg link_b_default[] = {
	{0x0100, 0xf2},
	{0x0101, 0x4a},
	{0x0007, 0x07},
	{0x0002, 0x13},
	{0x0010, 0x31},
	{0xffff, 0x64},
	{0xffff, 0x64},
	{0x01c8, 0x82},
	{0x01cd, 0x48},
	{0x01ce, 0xd8},
	{0x01cf, 0x70},
	{0x01d0, 0x02},
	{0x01d1, 0xaf},
	{0x01d2, 0x80},
	{0xffff, 0x64},
	{0x02d6, 0x84},
};

static const struct max96722_reg link_a_default[] = {
	/* disable local CC */
	{0x0001, 0xe4},
	{0x0012, 0x10},
	{0x0318, 0x5e},
	{0x02bf, 0x60},
};

static const struct max96722_reg_list link_settings[MAX96722_N_SINKS] = {
	[MAX_PORT_SIOA] = {
		.num_of_regs = ARRAY_SIZE(link_a_default),
		.regs = link_a_default,
	},
	[MAX_PORT_SIOB] = {
		.num_of_regs = ARRAY_SIZE(link_b_default),
		.regs = link_b_default,
	},
	[MAX_PORT_SIOC] = {
		.num_of_regs = 0,
		.regs = NULL,
	},
	[MAX_PORT_SIOD] = {
		.num_of_regs = 0,
		.regs = NULL,
	},
};

static const s64 max96722_link_freq[] = {
	400000000,
};

static void set_sub_stream_fmt(int index, u32 code)
{
	max96722_query_sub_stream[index] &= 0xFFFFFFFFFFFF0000;
	max96722_query_sub_stream[index] |= code;
}

static void set_sub_stream_h(int index, u32 height)
{
	s64 val = height & 0xFFFF;

	max96722_query_sub_stream[index] &= 0xFFFFFFFF0000FFFF;
	max96722_query_sub_stream[index] |= val << 16;
}

static void set_sub_stream_w(int index, u32 width)
{
	s64 val = width & 0xFFFF;

	max96722_query_sub_stream[index] &= 0xFFFF0000FFFFFFFF;
	max96722_query_sub_stream[index] |= val << 32;
}

static void set_sub_stream_dt(int index, u32 dt)
{
	s64 val = dt & 0xFF;

	max96722_query_sub_stream[index] &= 0xFF00FFFFFFFFFFFF;
	max96722_query_sub_stream[index] |= val << 48;
}

static void set_sub_stream_vc_id(int index, u32 vc_id)
{
	s64 val = vc_id & 0xFF;

	max96722_query_sub_stream[index] &= 0x00FFFFFFFFFFFFFF;
	max96722_query_sub_stream[index] |= val << 56;
}

static u8 max96722_set_sub_stream[] = {
	0, 0, 0, 0
};

struct max96722_source {
	struct v4l2_subdev *sd;

	struct max96722_subdev_info *subdev_info;
};

/*
 * fixed mapping
 * SIOA - vc 0
 * SIOB - vc 1
 * SIOC - vc 2
 * SIOD - vc 3
 *
 * link/vc to subdev mapping is flexible
 */
struct max96722_priv {
	struct i2c_client *client;
	struct v4l2_subdev sd;
	struct media_pad pads[MAX96722_N_PADS];

	struct regmap *regmap16;

	struct v4l2_ctrl_handler ctrls;

	struct v4l2_mbus_framefmt fmt[MAX96722_N_SINKS];

	struct mutex mutex;

	int errb_int;
	int lock_int;

	unsigned int nsources;
	unsigned int source_mask;
	unsigned int bound_sources;
	unsigned int stream_count;
	struct max96722_source sources[MAX96722_NUM_GMSL];

	struct max96722_platform_data *platform_data;

};

static struct max96722_subdev_info *port_to_subdev_info(
		struct max96722_priv *priv, int rx_port)
{
	int i;
	struct max96722_platform_data *pdata = priv->platform_data;

	for (i = 0; i < pdata->subdev_num; i++) {
		struct max96722_subdev_info *info = &pdata->subdev_info[i];

		if (info->rx_port == rx_port)
			return info;
	}

	return NULL;
}

static int max96722_read(struct max96722_priv *priv, u32 reg, u32 *val)
{
	int ret;

	ret = regmap_read(priv->regmap16, reg, val);

	if (ret) {
		dev_err(&priv->client->dev,
				"%s : register 0x%02x read failed (%d)\n",
				__func__, reg, ret);
	}

	return ret;
}

static int max96722_write(struct max96722_priv *priv, u32 reg, u32 val)
{
	int ret;

	ret = regmap_write(priv->regmap16, reg, val);

	if (ret) {
		dev_err(&priv->client->dev,
				"%s : register 0x%02x write failed (%d)\n",
				__func__, reg, ret);
	}

	return ret;
}

static int max96722_read_rem(struct max96722_priv *priv, u16 addr, u32 reg, u32 *val)
{
	int ret;
	unsigned short addr_backup;

	addr_backup = priv->client->addr;
	priv->client->addr = addr;
	ret = regmap_read(priv->regmap16, reg, val);
	priv->client->addr = addr_backup;

	if (ret < 0) {
		dev_err(&priv->client->dev,
				"%s : addr 0x%x register 0x%02x read failed (%d)\n",
				__func__, addr, reg, ret);
	}

	return ret;
}

static int max96722_write_rem(struct max96722_priv *priv, u16 addr, u32 reg, u32 val)
{
	int ret;
	unsigned short addr_backup;

	addr_backup = priv->client->addr;
	priv->client->addr = addr;
	ret = regmap_write(priv->regmap16, reg, val);
	priv->client->addr = addr_backup;

	if (ret) {
		dev_err(&priv->client->dev,
				"%s : addr 0x%x register 0x%02x write failed (%d)\n",
				__func__, addr, reg, ret);
	}

	return ret;
}

static int max96722_write_reg_list(struct max96722_priv *priv,
		const struct max96722_reg_list *r_list)
{
	int ret, i;

	for (i = 0; i < r_list->num_of_regs; i++) {
		if (r_list->regs[i].address == 0xffff) {
			msleep(r_list->regs[i].val);
			continue;
		}

		ret = max96722_write(priv, r_list->regs[i].address,
				r_list->regs[i].val);

		if (ret) {
			dev_err(&priv->client->dev,
					"%s : register list write failed @ (%d)\n",
					__func__, i);
			return ret;
		}
	}

	return 0;
}

static int max96722_write_rem_reg_list(struct max96722_priv *priv, u16 addr,
		const struct max96722_reg_list *r_list)
{
	int ret, i;

	for (i = 0; i < r_list->num_of_regs; i++) {
		if (r_list->regs[i].address == 0xffff) {
			msleep(r_list->regs[i].val);
			continue;
		}

		ret = max96722_write_rem(priv, addr, r_list->regs[i].address,
				r_list->regs[i].val);

		if (ret) {
			dev_err(&priv->client->dev,
					"%s : register list write failed @ (%d)\n",
					__func__, i);
			return ret;
		}
	}

	return 0;
}

static int max96722_s_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

/*
 * new interface, enable GMSL link
 */
static int max96722_s_stream_vc(struct max96722_priv *priv, u8 vc_id, u8 state)
{
	int ret;
	struct v4l2_subdev *sd;

	if (!(priv->bound_sources & BIT(vc_id))) {
		dev_err(&priv->client->dev, "No device on link %d\n", vc_id);
		return -EIO;
	}

	sd = priv->sources[vc_id].sd;

	ret = v4l2_subdev_call(sd, video, s_stream, state);
	if (ret) {
		dev_err(&priv->client->dev,
				"Fail to s_stream for %s, enable %d ret %d\n",
				sd->name, state, ret);
		return ret;
	}

	if (state) {
		if (priv->stream_count) {
			priv->stream_count++;
			return 0;
		}

		/* force mipi clocks running */
		dev_dbg(&priv->client->dev, "power on MIPI\n");
		max96722_write(priv, 0x8a0, 0x04);
		max96722_write(priv, 0x8a0, 0x84);
		priv->stream_count++;
	} else {
		priv->stream_count--;
		if (priv->stream_count)
			return 0;

		dev_dbg(&priv->client->dev, "power off MIPI\n");
		max96722_write(priv, 0x8a0, 0x04);
	}

	return 0;
}

static struct v4l2_mbus_framefmt *
max96722_get_pad_format(struct max96722_priv *priv,
		struct v4l2_subdev_state *sd_state,
		unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&priv->sd, sd_state, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &priv->fmt[pad];
	default:
		return NULL;
	}
}

/*
 * dt and bpp maybe different for each locked link
 * use the first locked sink pad's format for the source pad
 */
static int max96722_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_format *format)
{
	struct max96722_priv *priv = to_max96722(sd);
	unsigned int pad = format->pad;
	struct v4l2_mbus_framefmt *cfg_fmt;

	mutex_lock(&priv->mutex);

	if (pad == MAX96722_SRC_PAD)
		pad = __ffs(priv->bound_sources);

	cfg_fmt = max96722_get_pad_format(priv, sd_state, pad, format->which);
	if (!cfg_fmt) {
		dev_err(sd->dev, "Failed to find format info for pad %d\n", pad);
		mutex_unlock(&priv->mutex);
		return -EINVAL;
	}

	format->format = *cfg_fmt;

	mutex_unlock(&priv->mutex);

	return 0;
}

static int max96722_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_state *sd_state,
		struct v4l2_subdev_format *format)
{
	struct max96722_priv *priv = to_max96722(sd);
	unsigned int pad = format->pad;
	struct v4l2_mbus_framefmt *cfg_fmt;

	if (pad == MAX96722_SRC_PAD)
		return -EINVAL;

	mutex_lock(&priv->mutex);

	cfg_fmt = max96722_get_pad_format(priv, sd_state, pad, format->which);
	if (!cfg_fmt) {
		dev_err(sd->dev, "Failed to find format info for pad %d\n", pad);
		mutex_unlock(&priv->mutex);
		return -EINVAL;
	}

	*cfg_fmt = format->format;

	set_sub_stream_fmt(pad, cfg_fmt->code);
	set_sub_stream_h(pad, cfg_fmt->height);
	set_sub_stream_w(pad, cfg_fmt->width);
	set_sub_stream_dt(pad, mbus_code_to_mipi(cfg_fmt->code));
	set_sub_stream_vc_id(pad, pad);

	mutex_unlock(&priv->mutex);

	return 0;
}

static void max96722_init_format(struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = 1920;
	fmt->height = 1080;
	fmt->code = MEDIA_BUS_FMT_UYVY8_1X16;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int max96722_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format;
	int i;

	for (i = 0; i < MAX96722_N_SINKS; i++) {
		format = v4l2_subdev_get_try_format(sd, fh->state, i);
		max96722_init_format(format);
	}

	return 0;
}

static int detect_device(struct max96722_priv *priv,
		unsigned int rx_port, unsigned int i2c_addr, unsigned int ser)
{
	int ret;
	unsigned int val;

	if (!(priv->source_mask & BIT(rx_port))) {
		dev_info(&priv->client->dev, "Link %d is not locked\n", rx_port);
		return -ENXIO;
	}

	ret = max96722_read_rem(priv, i2c_addr, 0x0D, &val);
	if (ret) {
		dev_info(&priv->client->dev, "Failed to remote read %d", ret);
		return ret;
	}

	switch (ser) {
	case MAX_SER_9295A:
		ret = val != ID_9295A;
		break;
	case MAX_SER_96717F:
		ret = val != ID_96717F;
		break;
	default:
		dev_info(&priv->client->dev, "Unknown remote device type %d\n", ser);
		return -EINVAL;
	}

	if (ret) {
		dev_err(&priv->client->dev, "incompatible remot device connected %x\n", val);
		return -ENXIO;
	}

	return 0;
}

static int max96722_registered(struct v4l2_subdev *sd)
{
	int ret;
	struct max96722_priv *priv = to_max96722(sd);
	int i;
	int src_pad;
	unsigned int rx_port;

	for (i = 0; i < priv->platform_data->subdev_num && i < MAX96722_N_SINKS; i++) {
		struct max96722_subdev_info *info = &priv->platform_data->subdev_info[i];

		rx_port = info->rx_port;

		ret = detect_device(priv, rx_port, info->alias_addr,
				info->ser_type);
		if (ret) {
			dev_info(sd->dev, "Failed to detect remote dev %d\n", i);
			continue;
		}

		priv->sources[rx_port].sd = v4l2_i2c_new_subdev_board(
				priv->sd.v4l2_dev, priv->client->adapter,
				&info->board_info, 0);

		if (!priv->sources[rx_port].sd) {
			dev_err(sd->dev, "Failed to init remote dev %d\n", i);
			continue;
		}

		src_pad = media_get_pad_index(&priv->sources[rx_port].sd->entity,
				false, PAD_SIGNAL_DEFAULT);
		if (src_pad < 0) {
			dev_err(sd->dev, "Failed to find source pad on %s\n",
					priv->sources[rx_port].sd->name);
			return ret;
		}

		ret = media_create_pad_link(&priv->sources[rx_port].sd->entity, src_pad,
				&priv->sd.entity, info->rx_port, MEDIA_LNK_FL_DYNAMIC);
		if (ret) {
			dev_err(sd->dev, "Failed to creaet link %s:%d -> %s:%d\n",
					priv->sources[i].sd->name, src_pad,
					priv->sd.name, rx_port);
			return ret;
		}

		priv->sources[rx_port].subdev_info = info;
		priv->nsources++;
		priv->bound_sources |= BIT(info->rx_port);
	}

	return 0;
}

static const struct v4l2_subdev_video_ops max96722_video_ops = {
	.s_stream = max96722_s_stream,
};

static const struct v4l2_subdev_pad_ops max96722_pad_ops = {
	.get_fmt = max96722_get_fmt,
	.set_fmt = max96722_set_fmt,
};

static const struct v4l2_subdev_ops max96722_subdev_ops = {
	.video = &max96722_video_ops,
	.pad = &max96722_pad_ops,
};

static const struct v4l2_subdev_internal_ops max96722_internal_ops = {
	.open = max96722_open,
	.registered = max96722_registered,
};

static const struct media_entity_operations max96722_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int max96722_get_locked_status(struct max96722_priv *priv, int link)
{
	u16 reg;
	int ret;
	int val;

	switch (link) {
	case MAX_PORT_SIOA:
		reg = 0x1A;
		break;
	case MAX_PORT_SIOB:
		reg = 0x0A;
		break;
	case MAX_PORT_SIOC:
		reg = 0x0B;
		break;
	case MAX_PORT_SIOD:
		reg = 0x0C;
		break;
	default:
		dev_err(&priv->client->dev, "invalid link %d\n", link);
		return 0;
	}

	ret = max96722_read(priv, reg, &val);
	if (ret) {
		dev_err(&priv->client->dev, "failed to get link status %d\n", link);
		return 0;
	}

	return ((val & 0x08) >> 3);
}

static int max96722_remote_init(struct max96722_priv *priv, int rx_port,
		const struct max96722_reg_list *init_setting)
{
	u32 val;
	int ret;
	struct max96722_subdev_info *info = port_to_subdev_info(priv, rx_port);
	unsigned short tmp_addr;

	if (!info) {
		dev_err(&priv->client->dev, "link %d disconnect\n", rx_port);
		return -EREMOTEIO;
	}

	ret = max96722_get_locked_status(priv, rx_port);

	if (!ret) {
		dev_info(&priv->client->dev, "link %d not locked\n", rx_port);
		return -EIO;
	}

	if (info->power_gpio == -1) {
		/* get current addr in use */
		if (max96722_read_rem(priv, info->phy_i2c_addr, 0x10, &val))
			tmp_addr = info->alias_addr;
		else
			tmp_addr = info->phy_i2c_addr;
		/* reset */
		max96722_read_rem(priv, tmp_addr, 0x10, &val);
		max96722_write_rem(priv, tmp_addr, 0x10, val | 0x80);
		msleep(DELAY_MS);
	} else {
		gpio_set_value(info->power_gpio, 1);
		msleep(DELAY_MS);
		gpio_set_value(info->power_gpio, 0);
		msleep(DELAY_MS);
	}

	/* assign new addr */
	max96722_write_rem(priv, info->phy_i2c_addr, 0x00,
			info->alias_addr << 1);

	/* initialize remote */
	ret = max96722_write_rem_reg_list(priv, info->alias_addr, init_setting);

	return ret;
}

/* V4L2 control IDs */
#define V4L2_CID_LINKA_STATUS (V4L2_CID_IPU_BASE + 6)
#define V4L2_CID_RESET_LINKA (V4L2_CID_IPU_BASE + 7)
#define V4L2_CID_LINKB_STATUS (V4L2_CID_IPU_BASE + 8)
#define V4L2_CID_RESET_LINKB (V4L2_CID_IPU_BASE + 9)
#define V4L2_CID_OMC_MODE (V4L2_CID_IPU_BASE + 10)

static const char * const max96722_link_status[] = {
	"not locked",
	"locked",
};

static const char * const omc_day_night_mode[] = {
	"adaptive day mode",
	"adaptive night node",
	"host control day mode",
	"host control night mode",
};

static char check_sum(char *buf, int buflen)
{
	int csum = 0;
	int i;

	for (i = 0; i < buflen; i++)
		csum += buf[i];

	return (char)csum;
}

static int omc_nd_mode(struct v4l2_ctrl *ctrl, bool set)
{
	struct max96722_priv *priv = container_of(ctrl->handler,
			struct max96722_priv, ctrls);
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);

	char m2s[16] = {0x90, 0x31, 0x0c, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xd2};
	char s2m[16] = {0};

	struct i2c_msg  xfer[] = {
		{
			.addr = 0x28,
			.flags = 0,
			.len = 16,
			.buf = m2s,
		},
		{
			.addr = 0x28,
			.flags = I2C_M_RD,
			.len = 7,
			.buf = s2m,
		},
	};

	if (set) {
		m2s[3] = 0x06;
		m2s[4] = (ctrl->val >> 1) & 0x01;
		m2s[5] = ctrl->val & 0x01;
		m2s[15] = check_sum(m2s, 15);
		i2c_transfer(client->adapter, &xfer[0], 1);
	} else {
		m2s[15] = check_sum(m2s, 15);
		i2c_transfer(client->adapter, &xfer[0], 1);
		msleep(300);
		i2c_transfer(client->adapter, &xfer[1], 1);
		ctrl->val = s2m[4] * 2 + s2m[5];
	}

	return 0;
}

static int max96722_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct max96722_priv *priv = container_of(ctrl->handler,
			struct max96722_priv, ctrls);
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);
	u32 val;
	u8 vc_id;
	u8 state;
	int ret = 0;

	switch (ctrl->id) {
	case  V4L2_CID_IPU_SET_SUB_STREAM:
		val = (*ctrl->p_new.p_s64 & 0xffff);
		vc_id = (val >> 8) & 0xff;
		state = val & 0xff;

		max96722_set_sub_stream[vc_id] = state;

		ret = max96722_s_stream_vc(priv, vc_id, state);
		break;
	case V4L2_CID_RESET_LINKA:
		ret = max96722_remote_init(priv, MAX_PORT_SIOA, &link_settings[MAX_PORT_SIOA]);
		break;
	case V4L2_CID_RESET_LINKB:
		ret = max96722_remote_init(priv, MAX_PORT_SIOB, &link_settings[MAX_PORT_SIOB]);
		break;
	case V4L2_CID_OMC_MODE:
		ret = omc_nd_mode(ctrl, true);
		break;
	default:
		dev_info(&client->dev, "%s : v4l2 control id 0x%x\n", __func__, ctrl->id);
	}

	return ret;
}

static int max96722_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct max96722_priv *priv = container_of(ctrl->handler,
			struct max96722_priv, ctrls);
	struct i2c_client *client = priv->client;

	switch (ctrl->id) {
	case V4L2_CID_LINKA_STATUS:
		ctrl->val = max96722_get_locked_status(priv, MAX_PORT_SIOA);
		break;
	case V4L2_CID_LINKB_STATUS:
		ctrl->val = max96722_get_locked_status(priv, MAX_PORT_SIOB);
		break;
	case V4L2_CID_OMC_MODE:
		omc_nd_mode(ctrl, false);
		break;
	default:
		dev_info(&client->dev, "%s : v4l2 control id 0x%x\n", __func__, ctrl->id);
	}

	return 0;
}

static const struct v4l2_ctrl_ops max96722_ctrl_ops = {
	.g_volatile_ctrl = max96722_g_volatile_ctrl,
	.s_ctrl = max96722_s_ctrl,
};

static const struct v4l2_ctrl_config max96722_controls[] = {
	{
		.ops = &max96722_ctrl_ops,
		.id = V4L2_CID_LINK_FREQ,
		.name = "V4L2_CID_LINK_FREQ",
		.type = V4L2_CTRL_TYPE_INTEGER_MENU,
		.min = 0,
		.max = ARRAY_SIZE(max96722_link_freq) - 1,
		.def = 0,
		.menu_skip_mask = 0,
		.qmenu_int = max96722_link_freq,
	},
	{
		.ops = &max96722_ctrl_ops,
		.id = V4L2_CID_IPU_QUERY_SUB_STREAM,
		.name = "query virtual channel",
		.type = V4L2_CTRL_TYPE_INTEGER_MENU,
		.max = ARRAY_SIZE(max96722_query_sub_stream) - 1,
		.min = 0,
		.def = 0,
		.menu_skip_mask = 0,
		.qmenu_int = max96722_query_sub_stream,
	},
	{
		.ops = &max96722_ctrl_ops,
		.id = V4L2_CID_IPU_SET_SUB_STREAM,
		.name = "set virtual channel",
		.type = V4L2_CTRL_TYPE_INTEGER64,
		.max = 0xffff,
		.min = 0,
		.def = 0,
		.step = 1,
	},
	{
		.ops = &max96722_ctrl_ops,
		.id = V4L2_CID_LINKA_STATUS,
		.name = "query SIOA link status",
		.type = V4L2_CTRL_TYPE_MENU,
		.max = ARRAY_SIZE(max96722_link_status) - 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
		.qmenu = max96722_link_status,
	},
	{
		.ops = &max96722_ctrl_ops,
		.id = V4L2_CID_RESET_LINKA,
		.name = "reset SIOA",
		.type = V4L2_CTRL_TYPE_BUTTON,
	},
	{
		.ops = &max96722_ctrl_ops,
		.id = V4L2_CID_LINKB_STATUS,
		.name = "query SIOB link status",
		.type = V4L2_CTRL_TYPE_MENU,
		.max = ARRAY_SIZE(max96722_link_status) - 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY,
		.qmenu = max96722_link_status,
	},
	{
		.ops = &max96722_ctrl_ops,
		.id = V4L2_CID_RESET_LINKB,
		.name = "reset SIOB",
		.type = V4L2_CTRL_TYPE_BUTTON,
	},
	{
		.ops = &max96722_ctrl_ops,
		.id = V4L2_CID_OMC_MODE,
		.name = "OMC day/night mode",
		.type = V4L2_CTRL_TYPE_MENU,
		.max = ARRAY_SIZE(omc_day_night_mode) - 1,
		.def = 0,
		.flags = V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_EXECUTE_ON_WRITE,
		.qmenu = omc_day_night_mode,
	},
};

static max96722_register_subdev(struct max96722_priv *priv)
{
	int ret;
	int i;

	v4l2_i2c_subdev_init(&priv->sd, priv->client, &max96722_subdev_ops);
	snprintf(priv->sd.name, sizeof(priv->sd.name), "max96722");
	priv->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	priv->sd.internal_ops = &max96722_internal_ops;
	priv->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	priv->sd.entity.ops = &max96722_subdev_entity_ops;

	v4l2_ctrl_handler_init(&priv->ctrls, 1);
	priv->sd.ctrl_handler = &priv->ctrls;

	for (i = 0; i < ARRAY_SIZE(max96722_controls); i++) {
		struct v4l2_ctrl *ctrl;

		ctrl = v4l2_ctrl_new_custom(&priv->ctrls, &max96722_controls[i], NULL);
		if (priv->ctrls.error) {
			dev_err(&priv->client->dev, "Failed to create ctrl %s %d\n",
					max96722_controls[i].name, priv->ctrls.error);
			ret = priv->ctrls.error;
			goto failed_out;
		}
	}

	for (i = 0; i < MAX96722_NUM_GMSL; i++)
		priv->pads[i].flags = MEDIA_PAD_FL_SINK;
	priv->pads[MAX96722_SRC_PAD].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&priv->sd.entity, MAX96722_N_PADS, priv->pads);
	if (ret) {
		dev_err(&priv->client->dev,
				"%s : Failed to init media entity\n", __func__);
		goto failed_out;
	}

	return 0;

failed_out:
	media_entity_cleanup(&priv->sd.entity);
	v4l2_ctrl_handler_free(&priv->ctrls);

	return ret;
}

#define MAX96722_LINK_STATUS \
	(max96722_get_locked_status(priv, MAX_PORT_SIOA) << MAX_PORT_SIOA \
	| max96722_get_locked_status(priv, MAX_PORT_SIOB) << MAX_PORT_SIOB \
	| max96722_get_locked_status(priv, MAX_PORT_SIOC) << MAX_PORT_SIOC \
	| max96722_get_locked_status(priv, MAX_PORT_SIOD) << MAX_PORT_SIOD)


static int max96722_poc_enable(struct max96722_priv *priv, bool enable)
{
	int i;

	for (i = 0; i < priv->platform_data->subdev_num; i++) {
		struct max96722_subdev_info *info = &priv->platform_data->subdev_info[i];

		if (info->power_gpio != -1) {
			if (enable)
				gpio_set_value(info->power_gpio, 0);
			else
				gpio_set_value(info->power_gpio, 1);
		}
	}

	return 0;
}

static int max96722_init(struct max96722_priv *priv)
{
	int ret;
	unsigned int val;
	int i;
	struct max96722_platform_data *pdata = priv->platform_data;

	/* chip identify */
	ret = max96722_read(priv, 0x0D, &val);
	if (ret) {
		dev_err(&priv->client->dev, "Failed to read reg %x %d\n",
				0xD, ret);
		return ret;
	}

	if (val != 0xA1) {
		dev_err(&priv->client->dev, "Failed to detect max96722 %x\n", val);
		return -ENXIO;
	}

	/* internal regualtor */
	ret = max96722_read(priv, 0x17, &val);
	max96722_write(priv, 0x17, val | 0x04);
	ret = max96722_read(priv, 0x19, &val);
	max96722_write(priv, 0x19, val | 0x10);

	/* power over coax */
	ret = max96722_poc_enable(priv, true);
	if (ret)
		return ret;
	msleep(DELAY_MS);

	/* RESET_ONESHOT A/B/C/D */
	ret = max96722_write(priv, 0x18, 0x0F);
	msleep(DELAY_MS);

	/* control channel */
	ret = max96722_read(priv, 0x01, &val);
	dev_info(&priv->client->dev, "CC settings %x\n", val);

	/* GMSL2 */
	ret = max96722_read(priv, 0x06, &val);
	dev_info(&priv->client->dev, "Link settings %x\n", val);

	/* Coax */
	ret = max96722_read(priv, 0x22, &val);
	dev_info(&priv->client->dev, "Cable settings %x\n", val);

	/* Link lock */
	priv->source_mask = MAX96722_LINK_STATUS;
	dev_info(&priv->client->dev, "Link status %x\n", priv->source_mask);

	if (!priv->source_mask) {
		dev_err(&priv->client->dev, "No remote devices connected\n");
		return -ENXIO;
	}

	/* setup link after power up*/
	for (i = 0; i < MAX96722_N_SINKS && i < pdata->subdev_num; i++) {
		struct max96722_subdev_info *info = &pdata->subdev_info[i];
		unsigned int rx_port = info->rx_port;
		unsigned short tmp_addr;

		if (priv->source_mask & BIT(rx_port)) {
			/*
			 * no need to disable/enable the link
			 * just enable/disable the remote control channel
			 */
			max96722_write(priv, 0x03, ~(1 << rx_port * 2));

			/*
			 * for external powered device reset device to known state
			 * for POC powered device just power up the device and it's
			 * in a clean state
			 */
			if (info->power_gpio == -1) {
				/* remote RESET_ALL */
				if (max96722_read_rem(priv, info->phy_i2c_addr, 0x10, &val))
					tmp_addr = info->alias_addr;
				else
					tmp_addr = info->phy_i2c_addr;
				max96722_read_rem(priv, tmp_addr, 0x10, &val);
				max96722_write_rem(priv, tmp_addr, 0x10, val | 0x80);
				msleep(DELAY_MS);
			}

			/* assign new address */
			max96722_write_rem(priv, info->phy_i2c_addr,
					0x00, info->alias_addr << 1);

			/* initial settings */
			if (link_settings[rx_port].num_of_regs)
				ret = max96722_write_rem_reg_list(priv, info->alias_addr,
						&link_settings[rx_port]);
		}
	}

	/* enable all CC and reset */
	max96722_write(priv, 0x03, 0xaa);
	max96722_write(priv, 0x18, 0x0F);
	msleep(DELAY_MS);

	/* Link lock */
	priv->source_mask = MAX96722_LINK_STATUS;
	dev_info(&priv->client->dev, "Link status %x\n", priv->source_mask);

	if (!priv->source_mask) {
		dev_err(&priv->client->dev, "No remote devices connected\n");
		return -ENXIO;
	}

	/* FSYNC */
	ret = max96722_write_reg_list(priv, &fsync_setting);

	/* CFGH {A/B/C/D} VIDEO {X/Y/Z/U} */
	/* VIDEO PIPE SEL */
	ret = max96722_write_reg_list(priv, &video_pipe_setting);

	/* VID RX */
	/* VRX */
	/* BACKTOP */
	ret = max96722_write_reg_list(priv, &backtop_setting);

	/* MIPI TX */
	ret = max96722_write_reg_list(priv, &mipi_ctrl_setting);

	/* MIPI PHY */
	ret = max96722_write_reg_list(priv, &mipi_phy_setting);

	return 0;
}

static irqreturn_t max96722_threaded_irq_fn(int irq, void *devid)
{
	struct max96722_priv *priv = devid;

	dev_dbg(&priv->client->dev, "IRQ triggered  %x\n", irq);

	return IRQ_HANDLED;
}

static int max96722_probe(struct i2c_client *client)
{
	struct max96722_priv *priv;
	int ret;
	int i;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;

	priv->regmap16 = devm_regmap_init_i2c(client, &config16);
	if (IS_ERR(priv->regmap16)) {
		dev_err(&client->dev, "%s : Failed to init regmap\n", __func__);
		return -EIO;
	}

	priv->platform_data = client->dev.platform_data;

	if (priv->platform_data->errb_gpio != -1) {
		ret = devm_gpio_request_one(&client->dev,
				priv->platform_data->errb_gpio,
				0, "ERRB PIN");
		if (ret) {
			dev_err(&client->dev, "request errb gpio failed %d\n", ret);
			return ret;
		}

		ret = gpio_direction_input(priv->platform_data->errb_gpio);
		if (ret) {
			dev_err(&client->dev, "Failed to set ERRB as input %d\n", ret);
			return ret;
		}

		priv->errb_int = gpio_to_irq(priv->platform_data->errb_gpio);

		ret = devm_request_threaded_irq(&client->dev, priv->errb_int,
				NULL, max96722_threaded_irq_fn,
				priv->platform_data->errb_gpio_flags,
				priv->platform_data->errb_gpio_name,
				priv);
		if (ret) {
			dev_err(&client->dev, "Failed to request ERRB IRQ %d\n", ret);
			return ret;
		}
	}

	if (priv->platform_data->lock_gpio != -1) {
		ret = devm_gpio_request_one(&client->dev,
				priv->platform_data->lock_gpio,
				0, "LOCK PIN");
		if (ret) {
			dev_err(&client->dev, "request lock gpio failed %d\n", ret);
			return ret;
		}

		ret = gpio_direction_input(priv->platform_data->lock_gpio);
		if (ret) {
			dev_err(&client->dev, "Failed to set LOCK as input %d\n", ret);
			return ret;
		}

		priv->lock_int = gpio_to_irq(priv->platform_data->lock_gpio);

		ret = devm_request_threaded_irq(&client->dev, priv->lock_int,
				NULL, max96722_threaded_irq_fn,
				priv->platform_data->errb_gpio_flags,
				priv->platform_data->errb_gpio_name,
				priv);
		if (ret) {
			dev_err(&client->dev, "Failed to request LOCK IRQ %d\n", ret);
			return ret;
		}
	}

	dev_info(&client->dev, "errb irq %x, lock irq %x\n",
			priv->errb_int, priv->lock_int);

	for (i = 0; i < priv->platform_data->subdev_num; i++) {
		struct max96722_subdev_info *info = &priv->platform_data->subdev_info[i];

		if (info->power_gpio != -1) {
			ret = devm_gpio_request_one(&client->dev, info->power_gpio,
					GPIOF_OUT_INIT_LOW, "poc gpio");
			if (ret) {
				dev_err(&client->dev, "Failed to request power gpio %d\n", ret);
				return ret;
			}
		}
	}

	mutex_init(&priv->mutex);

	ret = max96722_init(priv);
	if (ret) {
		dev_err(&client->dev, "failed to init max96722 %d\n", ret);
		goto probe_err;
	}

	ret = max96722_register_subdev(priv);
	if (ret) {
		dev_err(&priv->client->dev,
				"%s : failed to register subdev\n", __func__);
		goto probe_err;
	}

	return 0;

probe_err:

	mutex_destroy(&priv->mutex);

	return ret;
}

static int max96722_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct max96722_priv *priv = to_max96722(sd);

	mutex_destroy(&priv->mutex);

	v4l2_ctrl_handler_free(&priv->ctrls);
	media_entity_cleanup(&priv->sd.entity);
	v4l2_device_unregister_subdev(sd);

	return 0;
}

/* no power or clk control */
static int max96722_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct max96722_priv *priv = to_max96722(subdev);
	int ret;
	int i;

	for (i = 0; i < MAX96722_NUM_GMSL; i++)
		if (max96722_set_sub_stream[i]) {
			ret = max96722_s_stream_vc(priv, i, 0);
			if (ret)
				dev_err(&client->dev, "failed to stop link %d ret %d\n", i, ret);
		}

	return 0;
}

/* re-initialize the link and resume streaming if needed */
static int max96722_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct max96722_priv *priv = to_max96722(subdev);
	int ret;
	int i;

	ret = max96722_init(priv);
	if (ret) {
		dev_err(&client->dev, "%s : resume fail %d\n", __func__, ret);
		return ret;
	}

	for (i = 0; i < MAX96722_NUM_GMSL; i++)
		if (max96722_set_sub_stream[i]) {
			ret = max96722_s_stream_vc(priv, i, 1);
			if (ret)
				dev_err(&client->dev, "failed to start	link %d ret %d\n", i, ret);
		}

	return 0;
}

static const struct i2c_device_id max96722_id_table[] = {
	{"max96722", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, max96722_id_table);

static const struct dev_pm_ops max96722_pm_ops = {
	.suspend = max96722_suspend,
	.resume = max96722_resume,
};

static struct i2c_driver max96722_i2c_driver = {
	.driver = {
		.name = "max96722",
		.pm = &max96722_pm_ops,
	},
	.probe_new = max96722_probe,
	.remove = max96722_remove,
	.id_table = max96722_id_table,
};

module_i2c_driver(max96722_i2c_driver);

MODULE_DESCRIPTION("Maxim MAX96722 GMSL Deserializer Driver");
MODULE_AUTHOR("Intel");
MODULE_LICENSE("GPL");

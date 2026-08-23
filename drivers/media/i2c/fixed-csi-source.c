// SPDX-License-Identifier: GPL-2.0
/*
 * Fixed-format MIPI CSI-2 source stub.
 *
 * Represents an already-configured, free-running MIPI CSI-2 video source
 * that Linux has no control over - register access, mode selection, power
 * sequencing are all owned externally (e.g. by a microcontroller on the
 * carrier board). This driver's only job is to give
 * drivers/staging/media/imx/imx8-media-dev.c's register_sensor_entities()
 * a real, i2c-bound v4l2_subdev/media_entity to link into the capture
 * graph, and to report the source's fixed, hardcoded format so downstream
 * negotiation (mipi_csi_0 -> isi_0) has something to agree on.
 *
 * No i2c transactions are ever issued to the physical device - the "reg"
 * address on the devicetree node is a bookkeeping placeholder only,
 * required so of_find_i2c_device_by_node() in imx8-media-dev.c can find
 * a bound i2c_client for this node; it is not a real register interface.
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/videodev2.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-common.h>
#include <media/v4l2-subdev.h>

#define FIXED_CSI_SOURCE_WIDTH		1920
#define FIXED_CSI_SOURCE_HEIGHT		1080
#define FIXED_CSI_SOURCE_CODE		MEDIA_BUS_FMT_UYVY8_2X8
#define FIXED_CSI_SOURCE_FPS		60

struct fixed_csi_source {
	struct v4l2_subdev sd;
	struct media_pad pad;
};

static void fixed_csi_source_fill_fmt(struct v4l2_mbus_framefmt *fmt)
{
	memset(fmt, 0, sizeof(*fmt));
	fmt->width = FIXED_CSI_SOURCE_WIDTH;
	fmt->height = FIXED_CSI_SOURCE_HEIGHT;
	fmt->code = FIXED_CSI_SOURCE_CODE;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->xfer_func = V4L2_XFER_FUNC_DEFAULT;
}

static int fixed_csi_source_enum_mbus_code(struct v4l2_subdev *sd,
					    struct v4l2_subdev_state *state,
					    struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	code->code = FIXED_CSI_SOURCE_CODE;
	return 0;
}

static int fixed_csi_source_enum_frame_size(struct v4l2_subdev *sd,
					     struct v4l2_subdev_state *state,
					     struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index != 0 || fse->code != FIXED_CSI_SOURCE_CODE)
		return -EINVAL;

	fse->min_width = fse->max_width = FIXED_CSI_SOURCE_WIDTH;
	fse->min_height = fse->max_height = FIXED_CSI_SOURCE_HEIGHT;
	return 0;
}

static int fixed_csi_source_get_fmt(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_format *format)
{
	fixed_csi_source_fill_fmt(&format->format);
	return 0;
}

static int fixed_csi_source_set_fmt(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_format *format)
{
	/* Fixed source - nothing to negotiate, always report the real format */
	fixed_csi_source_fill_fmt(&format->format);
	return 0;
}

static int fixed_csi_source_g_frame_interval(struct v4l2_subdev *sd,
					      struct v4l2_subdev_frame_interval *fi)
{
	fi->interval.numerator = 1;
	fi->interval.denominator = FIXED_CSI_SOURCE_FPS;
	return 0;
}

static int fixed_csi_source_s_stream(struct v4l2_subdev *sd, int enable)
{
	/*
	 * No-op: the source chip is configured and streamed by an external
	 * microcontroller, not by Linux. This only satisfies callers that
	 * expect a source subdev to answer s_stream.
	 */
	return 0;
}

static int fixed_csi_source_link_setup(struct media_entity *entity,
					const struct media_pad *local,
					const struct media_pad *remote,
					u32 flags)
{
	return 0;
}

static const struct media_entity_operations fixed_csi_source_entity_ops = {
	.link_setup = fixed_csi_source_link_setup,
};

static const struct v4l2_subdev_video_ops fixed_csi_source_video_ops = {
	.s_stream = fixed_csi_source_s_stream,
	.g_frame_interval = fixed_csi_source_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops fixed_csi_source_pad_ops = {
	.enum_mbus_code = fixed_csi_source_enum_mbus_code,
	.enum_frame_size = fixed_csi_source_enum_frame_size,
	.get_fmt = fixed_csi_source_get_fmt,
	.set_fmt = fixed_csi_source_set_fmt,
};

static const struct v4l2_subdev_ops fixed_csi_source_subdev_ops = {
	.video = &fixed_csi_source_video_ops,
	.pad = &fixed_csi_source_pad_ops,
};

static int fixed_csi_source_probe(struct i2c_client *client)
{
	struct fixed_csi_source *priv;
	int ret;

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&priv->sd, client, &fixed_csi_source_subdev_ops);
	priv->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	priv->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	priv->sd.entity.ops = &fixed_csi_source_entity_ops;

	priv->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&priv->sd.entity, 1, &priv->pad);
	if (ret < 0)
		return ret;

	ret = v4l2_subdev_init_finalize(&priv->sd);
	if (ret < 0)
		goto err_entity_cleanup;

	ret = v4l2_async_register_subdev(&priv->sd);
	if (ret < 0)
		goto err_entity_cleanup;

	return 0;

err_entity_cleanup:
	media_entity_cleanup(&priv->sd.entity);
	return ret;
}

static void fixed_csi_source_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
}

static const struct of_device_id fixed_csi_source_dt_ids[] = {
	{ .compatible = "fixed-mipi-csi-source" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fixed_csi_source_dt_ids);

static struct i2c_driver fixed_csi_source_i2c_driver = {
	.driver = {
		.name = "fixed-csi-source",
		.of_match_table = fixed_csi_source_dt_ids,
	},
	.probe = fixed_csi_source_probe,
	.remove = fixed_csi_source_remove,
};

module_i2c_driver(fixed_csi_source_i2c_driver);

MODULE_DESCRIPTION("Fixed-format MIPI CSI-2 source stub for externally-configured video sources");
MODULE_LICENSE("GPL");

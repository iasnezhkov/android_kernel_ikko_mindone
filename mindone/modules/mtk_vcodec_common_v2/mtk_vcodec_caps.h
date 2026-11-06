/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _MTK_VCODEC_CAPS_H_
#define _MTK_VCODEC_CAPS_H_

#include "vcodec_ipi_msg.h"

static inline bool mtk_vcodec_formats_valid(const struct mtk_video_fmt *formats,
					    size_t count, unsigned int codec_type,
					    unsigned int max_planes)
{
	bool has_codec = false, has_frame = false;
	size_t i;

	if (!count || !formats[0].fourcc)
		return false;
	for (i = 0; i < count; i++) {
		if (!formats[i].fourcc)
			continue;
		if (!formats[i].num_planes || formats[i].num_planes > max_planes)
			return false;
		if (formats[i].type == codec_type) {
			if (formats[i].num_planes != 1)
				return false;
			has_codec = true;
		} else if (formats[i].type == MTK_FMT_FRAME) {
			has_frame = true;
		} else {
			return false;
		}
	}
	return has_codec && has_frame;
}

static inline bool mtk_vcodec_framesizes_valid(const struct mtk_codec_framesizes *framesizes,
					       size_t count)
{
	const struct v4l2_frmsize_stepwise *s;
	size_t i;

	if (!count || !framesizes[0].fourcc)
		return false;
	for (i = 0; i < count; i++) {
		if (!framesizes[i].fourcc)
			continue;
		s = &framesizes[i].stepwise;
		if (!s->min_width || !s->min_height ||
		    s->min_width > s->max_width ||
		    s->min_height > s->max_height ||
		    !s->step_width || !s->step_height)
			return false;
	}
	return true;
}

#endif

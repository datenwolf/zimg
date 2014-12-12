#define ZIMG_PLANE_HELPER

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "zimg.h"
#include "VapourSynth.h"
#include "VSHelper.h"

#if ZIMG_API_VERSION < 2
  #error zAPI v1 or greater required
#endif

typedef enum chroma_location {
	CHROMA_LOC_MPEG1,
	CHROMA_LOC_MPEG2
} chroma_location;

static int translate_dither(const char *dither)
{
	if (!strcmp(dither, "none"))
		return ZIMG_DITHER_NONE;
	else if (!strcmp(dither, "ordered"))
		return ZIMG_DITHER_ORDERED;
	else if (!strcmp(dither, "random"))
		return ZIMG_DITHER_RANDOM;
	else if (!strcmp(dither, "error_diffusion"))
		return ZIMG_DITHER_ERROR_DIFFUSION;
	else
		return ZIMG_DITHER_NONE;
}

static int translate_pixel(const VSFormat *format)
{
	if (format->sampleType == stInteger && format->bytesPerSample == 1)
		return ZIMG_PIXEL_BYTE;
	else if (format->sampleType == stInteger && format->bytesPerSample == 2)
		return ZIMG_PIXEL_WORD;
	else if (format->sampleType == stFloat && format->bitsPerSample == 16)
		return ZIMG_PIXEL_HALF;
	else if (format->sampleType == stFloat && format->bitsPerSample == 32)
		return ZIMG_PIXEL_FLOAT;
	else
		return -1;
}

static int translate_filter(const char *filter)
{
	if (!strcmp(filter, "point"))
		return ZIMG_RESIZE_POINT;
	else if (!strcmp(filter, "bilinear"))
		return ZIMG_RESIZE_BILINEAR;
	else if (!strcmp(filter, "bicubic"))
		return ZIMG_RESIZE_BICUBIC;
	else if (!strcmp(filter, "spline16"))
		return ZIMG_RESIZE_SPLINE16;
	else if (!strcmp(filter, "spline36"))
		return ZIMG_RESIZE_SPLINE36;
	else if (!strcmp(filter, "lanczos"))
		return ZIMG_RESIZE_LANCZOS;
	else
		return ZIMG_RESIZE_POINT;
}

/* Offset needed to go from 4:4:4 to chroma location at given subsampling, relative to 4:4:4 grid. */
static double chroma_h_mpeg1_distance(const char *chroma_loc, int subsample)
{
	return (!strcmp(chroma_loc, "mpeg2") && subsample == 1) ? -0.5 : 0.0;
}

/* Adjustment to shift needed to convert between chroma locations. */
static double chroma_adjust_h(const char *loc_in, const char *loc_out, int subsample_in, int subsample_out)
{
	double scale = 1.0f / (double)(1 << subsample_in);
	double to_444_offset = -chroma_h_mpeg1_distance(loc_in, subsample_in) * scale;
	double from_444_offset = chroma_h_mpeg1_distance(loc_out, subsample_out) * scale;

	return to_444_offset + from_444_offset;
}

static double chroma_adjust_v(const char *loc_in, const char *loc_out, int subsample_in, int subsample_out)
{
	return 0.0;
}


typedef struct vs_colorspace_data {
	zimg_colorspace_context *colorspace_ctx;
	VSNodeRef *node;
	VSVideoInfo vi;
} vs_colorspace_data;

static void VS_CC vs_colorspace_init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi)
{
	const vs_colorspace_data *data = *instanceData;
	vsapi->setVideoInfo(&data->vi, 1, node);
}

static const VSFrameRef * VS_CC vs_colorspace_get_frame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
	vs_colorspace_data *data = *instanceData;
	VSFrameRef *ret = 0;
	char fail_str[1024] = { 0 };
	int err = 0;
	int p;

	zimg_clear_last_error();

	if (activationReason == arInitial) {
		vsapi->requestFrameFilter(n, data->node, frameCtx);
	} else if (activationReason == arAllFramesReady) {
		const VSFrameRef *src_frame = vsapi->getFrameFilter(n, data->node, frameCtx);
		VSFrameRef *dst_frame = 0;

		int width = vsapi->getFrameWidth(src_frame, 0);
		int height = vsapi->getFrameHeight(src_frame, 0);
		int pixel_type = translate_pixel(vsapi->getFrameFormat(src_frame));

		const void *src_plane[3];
		void *dst_plane[3];
		int src_stride[3];
		int dst_stride[3];

		size_t tmp_size;
		void *tmp = 0;

		dst_frame = vsapi->newVideoFrame(data->vi.format, width, height, src_frame, core);
		
		for (p = 0; p < 3; ++p) {
			src_plane[p] = vsapi->getReadPtr(src_frame, p);
			dst_plane[p] = vsapi->getWritePtr(dst_frame, p);
			src_stride[p] = vsapi->getStride(src_frame, p);
			dst_stride[p] = vsapi->getStride(dst_frame, p);
		}

		tmp_size = _zimg_colorspace_plane_tmp_size(data->colorspace_ctx, pixel_type);
		VS_ALIGNED_MALLOC(&tmp, tmp_size, 32);
		if (!tmp) {
			strcpy(fail_str, "error allocating temporary buffer");
			err = 1;
			goto fail;
		}

		_zimg_colorspace_plane_process(data->colorspace_ctx, src_plane, dst_plane, tmp, width, height, src_stride, dst_stride, pixel_type);

		ret = dst_frame;
		dst_frame = 0;
	fail:
		vsapi->freeFrame(src_frame);
		vsapi->freeFrame(dst_frame);
		VS_ALIGNED_FREE(tmp);
	}

	if (err)
		vsapi->setFilterError(fail_str, frameCtx);
	return ret;
}

static void VS_CC vs_colorspace_free(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
	vs_colorspace_data *data = instanceData;
	zimg_colorspace_delete(data->colorspace_ctx);
	vsapi->freeNode(data->node);
	free(data);
}

static void VS_CC vs_colorspace_create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
	vs_colorspace_data *data = 0;
	zimg_colorspace_context *colorspace_ctx = 0;
	char fail_str[1024] = { 0 };
	int err;

	VSNodeRef *node = 0;
	const VSVideoInfo *node_vi;
	const VSFormat *node_fmt;
	VSVideoInfo vi;

	int matrix_in;
	int transfer_in;
	int primaries_in;
	int matrix_out;
	int transfer_out;
	int primaries_out;

	zimg_clear_last_error();

	node = vsapi->propGetNode(in, "clip", 0, 0);
	node_vi = vsapi->getVideoInfo(node);
	node_fmt = node_vi->format;

	if (!node_fmt) {
		strcpy(fail_str, "clip must have a defined format");
		goto fail;
	}

	matrix_in = (int)vsapi->propGetInt(in, "matrix_in", 0, 0);
	transfer_in = (int)vsapi->propGetInt(in, "transfer_in", 0, 0);
	primaries_in = (int)vsapi->propGetInt(in, "primaries_in", 0, 0);

	matrix_out = (int)vsapi->propGetInt(in, "matrix_out", 0, &err);
	if (err)
		matrix_out = matrix_in;

	transfer_out = (int)vsapi->propGetInt(in, "transfer_out", 0, &err);
	if (err)
		transfer_out = transfer_in;

	primaries_out = (int)vsapi->propGetInt(in, "primaries_out", 0, &err);
	if (err)
		primaries_out = primaries_in;

	if (node_fmt->numPlanes < 3 || node_fmt->subSamplingW || node_fmt->subSamplingH) {
		strcpy(fail_str, "colorspace conversion can only be performed on 4:4:4 clips");
		goto fail;
	}

	vi = *node_vi;
	vi.format = vsapi->registerFormat(matrix_out == ZIMG_MATRIX_RGB ? cmRGB : cmYUV,
	                                  node_fmt->sampleType, node_fmt->bitsPerSample, node_fmt->subSamplingW, node_fmt->subSamplingH, core);

	colorspace_ctx = zimg_colorspace_create(matrix_in, transfer_in, primaries_in, matrix_out, transfer_out, primaries_out);
	if (!colorspace_ctx) {
		zimg_get_last_error(fail_str, sizeof(fail_str));
		goto fail;
	}
	if (!zimg_colorspace_pixel_supported(colorspace_ctx, translate_pixel(vi.format))) {
		strcpy(fail_str, "VSFormat not supported");
		goto fail;
	}

	data = malloc(sizeof(vs_colorspace_data));
	if (!data) {
		strcpy(fail_str, "error allocating vs_colorspace_data");
		goto fail;
	}

	data->colorspace_ctx = colorspace_ctx;
	data->node = node;
	data->vi = vi;

	vsapi->createFilter(in, out, "colorspace", vs_colorspace_init, vs_colorspace_get_frame, vs_colorspace_free, fmParallel, 0, data, core);
	return;
fail:
	vsapi->setError(out, fail_str);
	vsapi->freeNode(node);
	zimg_colorspace_delete(colorspace_ctx);
	free(data);
	return;
}


typedef struct vs_depth_data {
	zimg_depth_context *depth_ctx;
	VSNodeRef *node;
	VSVideoInfo vi;
	int tv_in;
	int tv_out;
} vs_depth_data;

static void VS_CC vs_depth_init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi)
{
	const vs_depth_data *data = *instanceData;
	vsapi->setVideoInfo(&data->vi, 1, node);
}

static const VSFrameRef * VS_CC vs_depth_get_frame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
	vs_depth_data *data = *instanceData;
	VSFrameRef *ret = 0;
	char fail_str[1024] = { 0 };
	int err = 0;
	int p;

	zimg_clear_last_error();

	if (activationReason == arInitial) {
		vsapi->requestFrameFilter(n, data->node, frameCtx);
	} else if (activationReason == arAllFramesReady) {
		const VSFrameRef *src_frame = vsapi->getFrameFilter(n, data->node, frameCtx);
		VSFrameRef *dst_frame = vsapi->newVideoFrame(data->vi.format, data->vi.width, data->vi.height, src_frame, core);

		const VSFormat *src_format = vsapi->getFrameFormat(src_frame);
		const VSFormat *dst_format = data->vi.format;

		int src_pixel = translate_pixel(src_format);
		int dst_pixel = translate_pixel(dst_format);
		int yuv = src_format->colorFamily == cmYUV || src_format->colorFamily == cmYCoCg;

		void *tmp = 0;
		size_t tmp_size = _zimg_depth_plane_tmp_size(data->depth_ctx, vsapi->getFrameWidth(src_frame, 0), src_pixel, dst_pixel);

		VS_ALIGNED_MALLOC(&tmp, tmp_size, 32);
		if (!tmp) {
			strcpy(fail_str, "error allocating temporary buffer");
			err = 1;
			goto fail;
		}

		for (p = 0; p < data->vi.format->numPlanes; ++p) {
			_zimg_depth_plane_process(data->depth_ctx,
			                          vsapi->getReadPtr(src_frame, p),
			                          vsapi->getWritePtr(dst_frame, p),
			                          tmp,
			                          vsapi->getFrameWidth(src_frame, p),
			                          vsapi->getFrameHeight(src_frame, p),
			                          vsapi->getStride(src_frame, p),
			                          vsapi->getStride(dst_frame, p),
			                          src_pixel,
			                          dst_pixel,
			                          src_format->bitsPerSample,
			                          dst_format->bitsPerSample,
			                          data->tv_in,
			                          data->tv_out,
			                          p > 0 && yuv);
			if (err) {
				zimg_get_last_error(fail_str, sizeof(fail_str));
				goto fail;
			}
		}
		ret = dst_frame;
		dst_frame = 0;
	fail:
		vsapi->freeFrame(src_frame);
		vsapi->freeFrame(dst_frame);
		VS_ALIGNED_FREE(tmp);
	}

	if (err)
		vsapi->setFilterError(fail_str, frameCtx);
	return ret;
}

static void VS_CC vs_depth_free(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
	vs_depth_data *data = instanceData;
	zimg_depth_delete(data->depth_ctx);
	vsapi->freeNode(data->node);
	free(data);
}

static void VS_CC vs_depth_create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
	vs_depth_data *data = 0;
	zimg_depth_context *depth_ctx = 0;
	char fail_str[1024] = { 0 };
	int err;

	VSNodeRef *node = 0;
	const VSVideoInfo *node_vi;
	const VSFormat *node_fmt;

	VSVideoInfo out_vi;
	const VSFormat *out_fmt;

	const char *dither;
	int sample;
	int depth;
	int tv_in;
	int tv_out;

	zimg_clear_last_error();

	node = vsapi->propGetNode(in, "clip", 0, 0);
	node_vi = vsapi->getVideoInfo(node);
	node_fmt = node_vi->format;

	if (!node_fmt) {
		strcpy(fail_str, "clip must have a defined format");
		goto fail;
	}

	dither = vsapi->propGetData(in, "dither", 0, &err);
	if (err)
		dither = "none";

	sample = (int)vsapi->propGetInt(in, "sample", 0, &err);
	if (err)
		sample = node_fmt->sampleType;

	depth = (int)vsapi->propGetInt(in, "depth", 0, &err);
	if (err)
		depth = node_fmt->bitsPerSample;

	tv_in = !!vsapi->propGetInt(in, "fullrange_in", 0, &err);
	if (err)
		tv_in = node_fmt->colorFamily == cmRGB;

	tv_out = !!vsapi->propGetInt(in, "fullrange_out", 0, &err);
	if (err)
		tv_out = node_fmt->colorFamily == cmRGB;

	if (sample != stInteger && sample != stFloat) {
		strcpy(fail_str, "invalid sample type: must be stInteger or stFloat");
		goto fail;
	}
	if (sample == stFloat && depth != 16 && depth != 32) {
		strcpy(fail_str, "only half and single-precision supported for floats");
		goto fail;
	}
	if (sample == stInteger && (depth <= 0 || depth > 16)) {
		strcpy(fail_str, "only bit depths 1-16 are supported for int");
		goto fail;
	}

	out_fmt = vsapi->registerFormat(node_fmt->colorFamily, sample, depth, node_fmt->subSamplingW, node_fmt->subSamplingH, core);
	out_vi.format = out_fmt;
	out_vi.fpsNum = node_vi->fpsNum;
	out_vi.fpsDen = node_vi->fpsDen;
	out_vi.width = node_vi->width;
	out_vi.height = node_vi->height;
	out_vi.numFrames = node_vi->numFrames;
	out_vi.flags = 0;

	depth_ctx = zimg_depth_create(translate_dither(dither));
	if (!depth_ctx) {
		zimg_get_last_error(fail_str, sizeof(fail_str));
		goto fail;
	}

	data = malloc(sizeof(vs_depth_data));
	if (!data) {
		strcpy(fail_str, "error allocating vs_depth_data");
		goto fail;
	}

	data->depth_ctx = depth_ctx;
	data->node = node;
	data->vi = out_vi;
	data->tv_in = tv_in;
	data->tv_out = tv_out;

	vsapi->createFilter(in, out, "depth", vs_depth_init, vs_depth_get_frame, vs_depth_free, fmParallel, 0, data, core);
	return;
fail:
	vsapi->setError(out, fail_str);
	vsapi->freeNode(node);
	zimg_depth_delete(depth_ctx);
	free(data);
	return;
}


typedef struct vs_resize_data {
	zimg_resize_context *resize_ctx_y_1;
	zimg_resize_context *resize_ctx_y_2;
	zimg_resize_context *resize_ctx_uv_1;
	zimg_resize_context *resize_ctx_uv_2;

	int use_y_as_uv;
	int tmp_width_y;
	int tmp_width_uv;
	int tmp_height_y;
	int tmp_height_uv;

	VSNodeRef *node;
	VSVideoInfo vi;
} vs_resize_data;

static void VS_CC vs_resize_init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi)
{
	const vs_resize_data *data = *instanceData;
	vsapi->setVideoInfo(&data->vi, 1, node);
}

static const VSFrameRef * VS_CC vs_resize_get_frame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
	vs_resize_data *data = *instanceData;
	VSFrameRef *ret = 0;
	char fail_str[1024] = { 0 };
	int err = 0;
	int p;

	zimg_clear_last_error();

	if (activationReason == arInitial) {
		vsapi->requestFrameFilter(n, data->node, frameCtx);
	} else if (activationReason == arAllFramesReady) {
		const VSFrameRef *src_frame = vsapi->getFrameFilter(n, data->node, frameCtx);
		VSFrameRef *dst_frame = vsapi->newVideoFrame(data->vi.format, data->vi.width, data->vi.height, src_frame, core);

		const VSFormat *format = data->vi.format;
		int pixel_type = translate_pixel(format);

		void *tmp = 0;
		size_t tmp_size = 0;

		for (p = 0; p < format->numPlanes; ++p) {
			int uv = p == 1 || p == 2;

			zimg_resize_context *resize_1 = (uv && !data->use_y_as_uv) ? data->resize_ctx_uv_1 : data->resize_ctx_y_1;
			zimg_resize_context *resize_2 = (uv && !data->use_y_as_uv) ? data->resize_ctx_uv_2 : data->resize_ctx_y_2;

			int src_width = vsapi->getFrameWidth(src_frame, p);
			int src_height = vsapi->getFrameHeight(src_frame, p);

			int dst_width = data->vi.width >> (uv ? data->vi.format->subSamplingW : 0);
			int dst_height = data->vi.height >> (uv ? data->vi.format->subSamplingH : 0);

			size_t local_sz = 0;
			
			if (resize_1 && resize_2) {
				int tmp_width = (uv && !data->use_y_as_uv) ? data->tmp_width_uv : data->tmp_width_y;
				int tmp_height = (uv && !data->use_y_as_uv) ? data->tmp_height_uv : data->tmp_height_y;

				size_t sz1 = _zimg_resize_plane_tmp_size(resize_1, src_width, src_height, tmp_width, tmp_height, pixel_type);
				size_t sz2 = _zimg_resize_plane_tmp_size(resize_2, tmp_width, tmp_height, dst_width, dst_height, pixel_type);

				local_sz = sz1 > sz2 ? sz1 : sz2;
			} else if (resize_1) {
				local_sz = _zimg_resize_plane_tmp_size(resize_1, src_width, src_height, dst_width, dst_height, pixel_type);
			}

			tmp_size = tmp_size > local_sz ? tmp_size : local_sz;
		}

		VS_ALIGNED_MALLOC(&tmp, tmp_size, 32);
		if (!tmp) {
			strcpy(fail_str, "error allocating temporary buffer");
			err = 1;
			goto fail;
		}

		for (p = 0; p < format->numPlanes; ++p) {
			int uv = p == 1 || p == 2;

			zimg_resize_context *resize_1 = (uv && !data->use_y_as_uv) ? data->resize_ctx_uv_1 : data->resize_ctx_y_1;
			zimg_resize_context *resize_2 = (uv && !data->use_y_as_uv) ? data->resize_ctx_uv_2 : data->resize_ctx_y_2;

			int src_width = vsapi->getFrameWidth(src_frame, p);
			int src_height = vsapi->getFrameHeight(src_frame, p);
			int src_stride = vsapi->getStride(src_frame, p);

			int dst_width = data->vi.width >> (uv ? data->vi.format->subSamplingW : 0);
			int dst_height = data->vi.height >> (uv ? data->vi.format->subSamplingH : 0);
			int dst_stride = vsapi->getStride(dst_frame, p);

			const void *src_p = vsapi->getReadPtr(src_frame, p);
			void *dst_p = vsapi->getWritePtr(dst_frame, p);

			if (resize_1 && resize_2) {
				int tmp_width = (uv && !data->use_y_as_uv) ? data->tmp_width_uv : data->tmp_width_y;
				int tmp_height = (uv && !data->use_y_as_uv) ? data->tmp_height_uv : data->tmp_height_y;

				const VSFormat *tmp_format = vsapi->registerFormat(cmGray, data->vi.format->sampleType, data->vi.format->bitsPerSample, 0, 0, core);
				VSFrameRef *tmp_frame = vsapi->newVideoFrame(tmp_format, tmp_width, tmp_height, 0, core);
				void *tmp_p = vsapi->getWritePtr(tmp_frame, 0);
				int tmp_stride = vsapi->getStride(tmp_frame, 0);

				_zimg_resize_plane_process(resize_1, src_p, tmp_p, tmp, src_width, src_height, tmp_width, tmp_height, src_stride, tmp_stride, pixel_type);
				_zimg_resize_plane_process(resize_2, tmp_p, dst_p, tmp, tmp_width, tmp_height, dst_width, dst_height, tmp_stride, dst_stride, pixel_type);

				vsapi->freeFrame(tmp_frame);
			} else if (resize_1) {
				_zimg_resize_plane_process(resize_1, src_p, dst_p, tmp, src_width, src_height, dst_width, dst_height, src_stride, dst_stride, pixel_type);
			} else {
				vs_bitblt(dst_p, dst_stride, src_p, src_stride, dst_width * data->vi.format->bytesPerSample, dst_height);
			}
		}
		ret = dst_frame;
		dst_frame = 0;
	fail:
		vsapi->freeFrame(src_frame);
		vsapi->freeFrame(dst_frame);
		VS_ALIGNED_FREE(tmp);
	}

	if (err)
		vsapi->setFilterError(fail_str, frameCtx);
	return ret;
}

static void VS_CC vs_resize_free(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
	vs_resize_data *data = instanceData;
	vsapi->freeNode(data->node);
	zimg_resize_delete(data->resize_ctx_y_1);
	zimg_resize_delete(data->resize_ctx_y_2);
	zimg_resize_delete(data->resize_ctx_uv_1);
	zimg_resize_delete(data->resize_ctx_uv_2);
	free(data);
}

static void VS_CC vs_resize_create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
	vs_resize_data *data = 0;
	zimg_resize_context *resize_ctx_y_h = 0;
	zimg_resize_context *resize_ctx_y_v = 0;
	zimg_resize_context *resize_ctx_uv_h = 0;
	zimg_resize_context *resize_ctx_uv_v = 0;
	char fail_str[1024] = { 0 };
	int err;

	VSNodeRef *node = 0;
	const VSVideoInfo *node_vi;
	const VSFormat *node_fmt;
	VSVideoInfo out_vi;
	const VSFormat *out_fmt;

	int width;
	int height;

	const char *filter;
	double filter_param_a;
	double filter_param_b;

	double shift_w;
	double shift_h;
	double subwidth;
	double subheight;

	const char *filter_uv;
	double filter_param_a_uv;
	double filter_param_b_uv;

	int subsample_w;
	int subsample_h;

	int skip_h_y;
	int skip_h_uv;
	int skip_v_y;
	int skip_v_uv;

	int hfirst_y;
	int hfirst_uv;
	int use_y_as_uv;

	const char *chroma_loc_in;
	const char *chroma_loc_out;

	node = vsapi->propGetNode(in, "clip", 0, 0);
	node_vi = vsapi->getVideoInfo(node);
	node_fmt = node_vi->format;

	if (!isConstantFormat(node_vi)) {
		strcpy(fail_str, "clip must have constant format");
		goto fail;
	}

	width = (int)vsapi->propGetInt(in, "width", 0, 0);
	height = (int)vsapi->propGetInt(in, "height", 0, 0);

	filter = vsapi->propGetData(in, "filter", 0, &err);
	if (err)
		filter = "point";

	filter_param_a = vsapi->propGetFloat(in, "filter_param_a", 0, &err);
	if (err)
		filter_param_a = NAN;

	filter_param_b = vsapi->propGetFloat(in, "filter_param_b", 0, &err);
	if (err)
		filter_param_b = NAN;

	shift_w = vsapi->propGetFloat(in, "shift_w", 0, &err);
	if (err)
		shift_w = 0.0;

	shift_h = vsapi->propGetFloat(in, "shift_h", 0, &err);
	if (err)
		shift_h = 0.0;

	subwidth = vsapi->propGetFloat(in, "subwidth", 0, &err);
	if (err)
		subwidth = node_vi->width;

	subheight = vsapi->propGetFloat(in, "subheight", 0, &err);
	if (err)
		subheight = node_vi->height;

	filter_uv = vsapi->propGetData(in, "filter_uv", 0, &err);
	if (err)
		filter_uv = filter;

	filter_param_a_uv = vsapi->propGetFloat(in, "filter_param_a_uv", 0, &err);
	if (err)
		filter_param_a_uv = !strcmp(filter, filter_uv) ? filter_param_a : NAN;

	filter_param_b_uv = vsapi->propGetFloat(in, "filter_param_b_uv", 0, &err);
	if (err)
		filter_param_b_uv = !strcmp(filter, filter_uv) ? filter_param_b : NAN;

	subsample_w = (int)vsapi->propGetInt(in, "subsample_w", 0, &err);
	if (err)
		subsample_w = node_fmt->subSamplingW;

	subsample_h = (int)vsapi->propGetInt(in, "subsample_h", 0, &err);
	if (err)
		subsample_h = node_fmt->subSamplingH;

	chroma_loc_in = vsapi->propGetData(in, "chroma_loc_in", 0, &err);
	if (err)
		chroma_loc_in = "mpeg2";

	chroma_loc_out = vsapi->propGetData(in, "chroma_loc_out", 0, &err);
	if (err)
		chroma_loc_out = "mpeg2";

	if (width <= 0 || height <= 0 || subwidth <= 0.0 || subheight <= 0.0) {
		strcpy(fail_str, "width and height must be positive");
		goto fail;
	}
	if ((node_fmt->colorFamily != cmYUV && node_fmt->colorFamily != cmYCoCg) && (subsample_w || subsample_h)) {
		strcpy(fail_str, "subsampling is only allowed for YUV");
		goto fail;
	}

	out_fmt = vsapi->registerFormat(node_fmt->colorFamily, node_fmt->sampleType, node_fmt->bitsPerSample, subsample_w, subsample_h, core);
	out_vi.format = out_fmt;
	out_vi.fpsNum = node_vi->fpsNum;
	out_vi.fpsDen = node_vi->fpsDen;
	out_vi.width = width;
	out_vi.height = height;
	out_vi.numFrames = node_vi->numFrames;
	out_vi.flags = 0;

	skip_h_y = node_vi->width == width && shift_w == 0.0 && subwidth == width;
	skip_v_y = node_vi->height == height && shift_h == 0.0 && subheight == height;

	if (!skip_h_y) {
		resize_ctx_y_h = zimg_resize_create(translate_filter(filter), 1, node_vi->width, width, shift_w, subwidth, filter_param_a, filter_param_b);
		if (!resize_ctx_y_h) {
			zimg_get_last_error(fail_str, sizeof(fail_str));
			goto fail;
		}
		if (!zimg_resize_pixel_supported(resize_ctx_y_h, translate_pixel(out_vi.format))) {
			strcpy(fail_str, "VSFormat not suported");
			goto fail;
		}
	}
	if (!skip_v_y) {
		resize_ctx_y_v = zimg_resize_create(translate_filter(filter), 0, node_vi->height, height, shift_h, subheight, filter_param_a, filter_param_b);
		if (!resize_ctx_y_v) {
			zimg_get_last_error(fail_str, sizeof(fail_str));
			goto fail;
		}
		if (!zimg_resize_pixel_supported(resize_ctx_y_v, translate_pixel(out_vi.format))) {
			strcpy(fail_str, "VSFormat not suported");
			goto fail;
		}
	}

	hfirst_y = zimg_resize_horizontal_first((double)node_vi->width / width, (double)node_vi->height / height);

	if (node_fmt->subSamplingW || node_fmt->subSamplingH || subsample_w || subsample_h) {
		int src_width_uv = node_vi->width >> node_fmt->subSamplingW;
		int src_height_uv = node_vi->height >> node_fmt->subSamplingH;
		int width_uv = width >> subsample_w;
		int height_uv = height >> subsample_h;

		double shift_w_uv = shift_w / (double)(1 << node_fmt->subSamplingW);
		double shift_h_uv = shift_h / (double)(1 << node_fmt->subSamplingH);
		double subwidth_uv = subwidth / (double)(1 << node_fmt->subSamplingW);
		double subheight_uv = subheight / (double)(1 << node_fmt->subSamplingH);

		shift_w_uv += chroma_adjust_h(chroma_loc_in, chroma_loc_out, node_fmt->subSamplingW, subsample_w);
		shift_h_uv += chroma_adjust_v(chroma_loc_in, chroma_loc_out, node_fmt->subSamplingH, subsample_h);

		skip_h_uv = src_width_uv == width_uv && shift_w_uv == 0.0 && subwidth_uv == width_uv;
		skip_v_uv = src_height_uv == height_uv && shift_h_uv == 0.0 && subheight_uv == height_uv;

		if (!skip_h_uv) {
			resize_ctx_uv_h = zimg_resize_create(translate_filter(filter_uv), 1, src_width_uv, width_uv, shift_w_uv, subwidth_uv, filter_param_a_uv, filter_param_b_uv);
			if (!resize_ctx_uv_h) {
				zimg_get_last_error(fail_str, sizeof(fail_str));
				goto fail;
			}
			if (!zimg_resize_pixel_supported(resize_ctx_uv_h, translate_pixel(out_vi.format))) {
				strcpy(fail_str, "VSFormat not suported");
				goto fail;
			}
		}
		if (!skip_v_uv) {
			resize_ctx_uv_v = zimg_resize_create(translate_filter(filter_uv), 0, src_height_uv, height_uv, shift_h_uv, subheight_uv, filter_param_a_uv, filter_param_b_uv);
			if (!resize_ctx_uv_v) {
				zimg_get_last_error(fail_str, sizeof(fail_str));
				goto fail;
			}
			if (!zimg_resize_pixel_supported(resize_ctx_uv_v, translate_pixel(out_vi.format))) {
				strcpy(fail_str, "VSFormat not suported");
				goto fail;
			}
		}

		hfirst_uv = zimg_resize_horizontal_first((double)src_width_uv / width_uv, (double)src_height_uv / height_uv);
		use_y_as_uv = 0;
	} else {
		skip_h_uv = 0;
		skip_v_uv = 0;

		hfirst_uv = 0;
		use_y_as_uv = 1;
	}

	data = malloc(sizeof(vs_resize_data));
	if (!data) {
		strcpy(fail_str, "error allocaing vs_resize_data");
		goto fail;
	}

	data->node = node;

	if (skip_h_y && skip_v_y) {
		data->resize_ctx_y_1 = 0;
		data->resize_ctx_y_2 = 0;
	} else if (skip_h_y) {
		data->resize_ctx_y_1 = resize_ctx_y_v;
		data->resize_ctx_y_2 = 0;
	} else if (skip_v_y) {
		data->resize_ctx_y_1 = resize_ctx_y_h;
		data->resize_ctx_y_2 = 0;
	} else {
		data->resize_ctx_y_1 = hfirst_y ? resize_ctx_y_h : resize_ctx_y_v;
		data->resize_ctx_y_2 = hfirst_y ? resize_ctx_y_v : resize_ctx_y_h;
	}

	data->use_y_as_uv = use_y_as_uv;

	if (!use_y_as_uv) {
		if (skip_h_uv && skip_v_uv) {
			data->resize_ctx_uv_1 = 0;
			data->resize_ctx_uv_2 = 0;
		} else if (skip_h_uv) {
			data->resize_ctx_uv_1 = resize_ctx_uv_v;
			data->resize_ctx_uv_2 = 0;
		} else if (skip_v_uv) {
			data->resize_ctx_uv_1 = resize_ctx_uv_h;
			data->resize_ctx_uv_2 = 0;
		} else {
			data->resize_ctx_uv_1 = hfirst_uv ? resize_ctx_uv_h : resize_ctx_uv_v;
			data->resize_ctx_uv_2 = hfirst_uv ? resize_ctx_uv_v : resize_ctx_uv_h;
		}
	}

	if (!skip_h_y && !skip_v_y) {
		data->tmp_width_y = hfirst_y ? width : node_vi->width;
		data->tmp_height_y = hfirst_y ? node_vi->height : height;
	} else {
		data->tmp_width_y = 0;
		data->tmp_height_y = 0;
	}

	if (!use_y_as_uv && !skip_h_uv && !skip_v_uv) {
		data->tmp_width_uv = hfirst_uv ? width >> subsample_w : node_vi->width >> node_vi->format->subSamplingW;
		data->tmp_height_uv = hfirst_uv ? node_vi->width >> node_vi->format->subSamplingH : height >> subsample_h;
	} else {
		data->tmp_width_uv = 0;
		data->tmp_height_uv = 0;
	}

	data->vi = out_vi;

	vsapi->createFilter(in, out, "resize", vs_resize_init, vs_resize_get_frame, vs_resize_free, fmParallel, 0, data, core);
	return;
fail:
	vsapi->freeNode(node);
	zimg_resize_delete(resize_ctx_y_h);
	zimg_resize_delete(resize_ctx_y_v);
	zimg_resize_delete(resize_ctx_uv_h);
	zimg_resize_delete(resize_ctx_uv_v);
	free(data);
	return;
}

static void VS_CC vs_set_cpu(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
	const char *cpu = vsapi->propGetData(in, "cpu", 0, 0);

	if (!strcmp(cpu, "none"))
		zimg_set_cpu(ZIMG_CPU_NONE);
	else if (!strcmp(cpu, "auto"))
		zimg_set_cpu(ZIMG_CPU_AUTO);
#if defined(__i386) || defined(_M_IX86) || defined(_M_X64) || defined(__x86_64__)
	else if (!strcmp(cpu, "mmx"))
		zimg_set_cpu(ZIMG_CPU_X86_MMX);
	else if (!strcmp(cpu, "sse"))
		zimg_set_cpu(ZIMG_CPU_X86_SSE);
	else if (!strcmp(cpu, "sse2"))
		zimg_set_cpu(ZIMG_CPU_X86_SSE2);
	else if (!strcmp(cpu, "sse3"))
		zimg_set_cpu(ZIMG_CPU_X86_SSE3);
	else if (!strcmp(cpu, "ssse3"))
		zimg_set_cpu(ZIMG_CPU_X86_SSSE3);
	else if (!strcmp(cpu, "sse41"))
		zimg_set_cpu(ZIMG_CPU_X86_SSE41);
	else if (!strcmp(cpu, "sse42"))
		zimg_set_cpu(ZIMG_CPU_X86_SSE42);
	else if (!strcmp(cpu, "avx"))
		zimg_set_cpu(ZIMG_CPU_X86_AVX);
	else if (!strcmp(cpu, "f16c"))
		zimg_set_cpu(ZIMG_CPU_X86_F16C);
	else if (!strcmp(cpu, "avx2"))
		zimg_set_cpu(ZIMG_CPU_X86_AVX2);
#endif
	else
		zimg_set_cpu(ZIMG_CPU_NONE);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
	configFunc("the.weather.channel", "z", "batman", VAPOURSYNTH_API_VERSION, 1, plugin);

	registerFunc("Colorspace", "clip:clip;"
	                           "matrix_in:int;"
	                           "transfer_in:int;"
	                           "primaries_in:int;"
	                           "matrix_out:int:opt;"
	                           "transfer_out:int:opt;"
	                           "primaries_out:int:opt", vs_colorspace_create, 0, plugin);
	registerFunc("Depth", "clip:clip;"
	                      "dither:data:opt;"
	                      "sample:int:opt;"
	                      "depth:int:opt;"
	                      "fullrange_in:int:opt;"
	                      "fullrange_out:int:opt", vs_depth_create, 0, plugin);
	registerFunc("Resize", "clip:clip;"
	                       "width:int;"
	                       "height:int;"
	                       "filter:data:opt;"
	                       "filter_param_a:float:opt;"
	                       "filter_param_b:float:opt;"
	                       "shift_w:float:opt;"
	                       "shift_h:float:opt;"
	                       "subwidth:float:opt;"
	                       "subheight:float:opt;"
	                       "filter_uv:data:opt;"
	                       "filter_param_a_uv:float:opt;"
	                       "filter_param_b_uv:float:opt;"
	                       "subsample_w:int:opt;"
	                       "subsample_h:int:opt;"
	                       "chroma_loc_in:data:opt;"
	                       "chroma_loc_out:data:opt;", vs_resize_create, 0, plugin);

	registerFunc("SetCPU", "cpu:data", vs_set_cpu, 0, plugin);

	zimg_set_cpu(ZIMG_CPU_AUTO);
}

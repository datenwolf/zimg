#ifndef ZIMGPLUSPLUS_H_
#define ZIMGPLUSPLUS_H_

#include "zimg.h"

struct ZimgError {
	int code;
	char msg[1024];

	ZimgError()
	{
		code = zimg_get_last_error(msg, sizeof(msg));
	}
};

class ZimgColorspaceContext {
	zimg_colorspace_context *m_ctx;
public:
	ZimgColorspaceContext(int matrix_in, int transfer_in, int primaries_in,
	                      int matrix_out, int transfer_out, int primaries_out)
	{
		if (!(m_ctx = zimg_colorspace_create(matrix_in, transfer_in, primaries_in, matrix_out, transfer_out, primaries_out)))
			throw ZimgError{};
	}

	ZimgColorspaceContext(const ZimgColorspaceContext &) = delete;

	ZimgColorspaceContext &operator=(const ZimgColorspaceContext &) = delete;

	~ZimgColorspaceContext()
	{
		zimg_colorspace_delete(m_ctx);
	}

	size_t tmp_size()
	{
		return zimg_colorspace_tmp_size(m_ctx);
	}

	int pixel_supported(int pixel_type)
	{
		return zimg_colorspace_pixel_supported(m_ctx, pixel_type);
	}

	void process_tile(const zimg_image_tile_t src[3], const zimg_image_tile_t dst[3], void *tmp, int pixel_type)
	{
		if (zimg_colorspace_process_tile(m_ctx, src, dst, tmp, pixel_type))
			throw ZimgError{};
	}
};

class ZimgDepthContext {
	zimg_depth_context *m_ctx;
public:
	ZimgDepthContext(int dither_type)
	{
		if (!(m_ctx = zimg_depth_create(dither_type)))
			throw ZimgError{};
	}

	ZimgDepthContext(const ZimgDepthContext &) = delete;

	ZimgDepthContext &operator=(const ZimgDepthContext &) = delete;

	~ZimgDepthContext()
	{
		zimg_depth_delete(m_ctx);
	}

	int tile_supported(int pixel_in, int pixel_out)
	{
		return zimg_depth_tile_supported(m_ctx, pixel_in, pixel_out);
	}

	size_t tmp_size(int width)
	{
		return zimg_depth_tmp_size(m_ctx, width);
	}

	void process(const zimg_image_tile_t *src, const zimg_image_tile_t *dst, void *tmp)
	{
		if (zimg_depth_process(m_ctx, src, dst, tmp))
			throw ZimgError{};
	}
};

class ZimgResizeContext {
	zimg_resize_context *m_ctx;
public:
	ZimgResizeContext(int filter_type, int horizontal, int src_dim, int dst_dim,
	                  double shift, double width, double filter_param_a, double filter_param_b)
	{
		if (!(m_ctx = zimg_resize_create(filter_type, horizontal, src_dim, dst_dim, shift, width, filter_param_a, filter_param_b)))
			throw ZimgError{};
	}

	ZimgResizeContext(const ZimgResizeContext &) = delete;

	ZimgResizeContext &operator=(const ZimgResizeContext &) = delete;

	~ZimgResizeContext()
	{
		zimg_resize_delete(m_ctx);
	}

	int pixel_supported(int pixel_type)
	{
		return zimg_resize_pixel_supported(m_ctx, pixel_type);
	}

	void dependent_rect(int dst_top, int dst_left, int dst_bottom, int dst_right,
	                    int *src_top, int *src_left, int *src_bottom, int *src_right)
	{
		zimg_resize_dependent_rect(m_ctx, dst_top, dst_left, dst_bottom, dst_right, src_top, src_left, src_bottom, src_right);
	}

	void process_tile(const zimg_image_tile_t *src, const zimg_image_tile_t *dst)
	{
		if (zimg_resize_process_tile(m_ctx, src, dst))
			throw ZimgError{};
	}
};

#endif // ZIMGPLUSPLUS_H_

#include <atomic>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>
#include "Common/cpuinfo.h"
#include "Common/except.h"
#include "Common/osdep.h"
#include "Common/pixel.h"
#include "Common/tile.h"
#include "Colorspace/colorspace.h"
#include "Colorspace/colorspace_param.h"
#include "Depth/depth.h"
#include "Resize/filter.h"
#include "Resize/resize.h"
#include "zimg.h"

using namespace zimg;

namespace {;

std::atomic<CPUClass> g_cpu_type{ CPUClass::CPU_NONE };
THREAD_LOCAL int g_last_error = 0;
THREAD_LOCAL char g_last_error_msg[1024];

CPUClass get_cpu_class(int cpu)
{
	switch (cpu) {
	case ZIMG_CPU_NONE:
		return CPUClass::CPU_NONE;
#ifdef ZIMG_X86
	case ZIMG_CPU_AUTO:
		return CPUClass::CPU_X86_AUTO;
	case ZIMG_CPU_X86_SSE2:
	case ZIMG_CPU_X86_SSE3:
	case ZIMG_CPU_X86_SSSE3:
	case ZIMG_CPU_X86_SSE41:
	case ZIMG_CPU_X86_SSE42:
	case ZIMG_CPU_X86_AVX:
	case ZIMG_CPU_X86_F16C:
		return CPUClass::CPU_X86_SSE2;
	case ZIMG_CPU_X86_AVX2:
		return CPUClass::CPU_X86_AVX2;
#endif
	default:
		return CPUClass::CPU_NONE;
	}
}

PixelType get_pixel_type(int pixel_type)
{
	switch (pixel_type) {
	case ZIMG_PIXEL_BYTE:
		return PixelType::BYTE;
	case ZIMG_PIXEL_WORD:
		return PixelType::WORD;
	case ZIMG_PIXEL_HALF:
		return PixelType::HALF;
	case ZIMG_PIXEL_FLOAT:
		return PixelType::FLOAT;
	default:
		throw ZimgIllegalArgument{ "unknown pixel type" };
	}
}

PlaneDescriptor get_plane_desc(const zimg_image_tile_t *tile)
{
	PixelFormat format{ get_pixel_type(tile->pixel_type), tile->depth, !!tile->range, !!tile->chroma };
	return{ format, tile->plane_width, tile->plane_height };
}

template <class T>
void get_image_tile(const zimg_image_tile_t *tile, ImageTile<T> *ztile, PlaneDescriptor *desc)
{
	*desc = get_plane_desc(tile);
	*ztile = ImageTile<T>{ static_cast<T *>(tile->buffer), desc, tile->stride };
}

colorspace::MatrixCoefficients get_matrix_coeffs(int matrix)
{
	switch (matrix) {
	case ZIMG_MATRIX_RGB:
		return colorspace::MatrixCoefficients::MATRIX_RGB;
	case ZIMG_MATRIX_709:
		return colorspace::MatrixCoefficients::MATRIX_709;
	case ZIMG_MATRIX_470BG:
	case ZIMG_MATRIX_170M:
		return colorspace::MatrixCoefficients::MATRIX_601;
	case ZIMG_MATRIX_2020_NCL:
		return colorspace::MatrixCoefficients::MATRIX_2020_NCL;
	case ZIMG_MATRIX_2020_CL:
		return colorspace::MatrixCoefficients::MATRIX_2020_CL;
	default:
		throw ZimgIllegalArgument{ "unknown matrix coefficients" };
	}
}

colorspace::TransferCharacteristics get_transfer_characteristics(int transfer)
{
	switch (transfer) {
	case ZIMG_TRANSFER_709:
	case ZIMG_TRANSFER_601:
	case ZIMG_TRANSFER_2020_10:
	case ZIMG_TRANSFER_2020_12:
		return colorspace::TransferCharacteristics::TRANSFER_709;
	case ZIMG_TRANSFER_LINEAR:
		return colorspace::TransferCharacteristics::TRANSFER_LINEAR;
	default:
		throw ZimgIllegalArgument{ "unknown transfer characteristics" };
	}
}

colorspace::ColorPrimaries get_color_primaries(int primaries)
{
	switch (primaries) {
	case ZIMG_PRIMARIES_709:
		return colorspace::ColorPrimaries::PRIMARIES_709;
	case ZIMG_PRIMARIES_170M:
	case ZIMG_PRIMARIES_240M:
		return colorspace::ColorPrimaries::PRIMARIES_SMPTE_C;
	case ZIMG_PRIMARIES_2020:
		return colorspace::ColorPrimaries::PRIMARIES_2020;
	default:
		throw ZimgIllegalArgument{ "unknown color primaries" };
	}
}

depth::DitherType get_dither_type(int dither)
{
	switch (dither) {
	case ZIMG_DITHER_NONE:
		return depth::DitherType::DITHER_NONE;
	case ZIMG_DITHER_ORDERED:
		return depth::DitherType::DITHER_ORDERED;
	case ZIMG_DITHER_RANDOM:
		return depth::DitherType::DITHER_RANDOM;
	case ZIMG_DITHER_ERROR_DIFFUSION:
		return depth::DitherType::DITHER_ERROR_DIFFUSION;
	default:
		throw ZimgIllegalArgument{ "unknown dither type" };
	}
}

resize::Filter *create_filter(int filter_type, double filter_param_a, double filter_param_b)
{
	switch (filter_type) {
	case ZIMG_RESIZE_POINT:
		return new resize::PointFilter{};
	case ZIMG_RESIZE_BILINEAR:
		return new resize::BilinearFilter{};
	case ZIMG_RESIZE_BICUBIC:
		filter_param_a = std::isfinite(filter_param_a) ? filter_param_a : 1.0 / 3.0;
		filter_param_b = std::isfinite(filter_param_b) ? filter_param_b : 1.0 / 3.0;
		return new resize::BicubicFilter{ filter_param_a, filter_param_b };
	case ZIMG_RESIZE_SPLINE16:
		return new resize::Spline16Filter{};
	case ZIMG_RESIZE_SPLINE36:
		return new resize::Spline36Filter{};
	case ZIMG_RESIZE_LANCZOS:
		filter_param_a = std::isfinite(filter_param_a) ? std::floor(filter_param_a) : 3.0;
		return new resize::LanczosFilter{ (int)filter_param_a };
	default:
		throw ZimgIllegalArgument{ "unknown resampling filter" };
	}
}

bool pointer_is_aligned(void *ptr)
{
#ifdef ZIMG_X86
	return !ptr || reinterpret_cast<uintptr_t>(ptr) % 32 == 0;
#else
	return true;
#endif
}

void handle_exception(const ZimgException &e)
{
	try {
		zimg_clear_last_error();

		std::strncpy(g_last_error_msg, e.what(), sizeof(g_last_error_msg));
		g_last_error_msg[sizeof(g_last_error_msg) - 1] = '\0';

		throw e;
	} catch (const ZimgUnknownError &) {
		g_last_error = ZIMG_ERROR_UNKNOWN;
	} catch (const ZimgLogicError &) {
		g_last_error = ZIMG_ERROR_LOGIC;
	} catch (const ZimgOutOfMemory &) {
		g_last_error = ZIMG_ERROR_OUT_OF_MEMORY;
	} catch (const ZimgIllegalArgument &) {
		g_last_error = ZIMG_ERROR_ILLEGAL_ARGUMENT;
	} catch (const ZimgUnsupportedError &) {
		g_last_error = ZIMG_ERROR_UNSUPPORTED;
	} catch (...) {
		g_last_error = ZIMG_ERROR_UNKNOWN;
	}
}

void handle_bad_alloc()
{
	zimg_clear_last_error();
	g_last_error = ZIMG_ERROR_OUT_OF_MEMORY;
}

} // namespace


int zimg_get_last_error(char *err_msg, size_t n)
{
	if (err_msg) {
		std::strncpy(err_msg, g_last_error_msg, std::min(sizeof(g_last_error_msg), n));
		err_msg[n - 1] = '\0';
	}

	return g_last_error;
}

void zimg_clear_last_error(void)
{
	g_last_error = 0;
	g_last_error_msg[0] = '\0';
}

void zimg_set_cpu(int cpu)
{
	try {
		g_cpu_type = get_cpu_class(cpu);
	} catch (const ZimgException &e) {
		handle_exception(e);
	}
}


struct zimg_colorspace_context {
	colorspace::ColorspaceConversion p;
};

zimg_colorspace_context *zimg_colorspace_create(int matrix_in, int transfer_in, int primaries_in,
                                                int matrix_out, int transfer_out, int primaries_out)
{
	zimg_colorspace_context *ret = nullptr;

	try {
		colorspace::ColorspaceDefinition csp_in;
		colorspace::ColorspaceDefinition csp_out;

		csp_in.matrix     = get_matrix_coeffs(matrix_in);
		csp_in.transfer   = get_transfer_characteristics(transfer_in);
		csp_in.primaries  = get_color_primaries(primaries_in);

		csp_out.matrix    = get_matrix_coeffs(matrix_out);
		csp_out.transfer  = get_transfer_characteristics(transfer_out);
		csp_out.primaries = get_color_primaries(primaries_out);

		ret = new zimg_colorspace_context{ colorspace::ColorspaceConversion{ csp_in, csp_out, g_cpu_type } };
	} catch (const ZimgException &e) {
		handle_exception(e);
	} catch (const std::bad_alloc &) {
		handle_bad_alloc();
	}

	return ret;
}

size_t zimg_colorspace_tmp_size(zimg_colorspace_context *ctx)
{
	assert(ctx);
	return ctx->p.tmp_size() * sizeof(float);
}

int zimg_colorspace_pixel_supported(zimg_colorspace_context *ctx, int pixel_type)
{
	int ret = 0;

	assert(ctx);

	try {
		ret = ctx->p.pixel_supported(get_pixel_type(pixel_type));
	} catch (const ZimgException &e) {
		handle_exception(e);
	}

	return ret;
}

int zimg_colorspace_process_tile(zimg_colorspace_context *ctx, const zimg_image_tile_t src[3], const zimg_image_tile_t dst[3], void *tmp, int pixel_type)
{
	int ret = 0;

	assert(ctx);
	assert(src && src[0].buffer && src[1].buffer && src[2].buffer);
	assert(dst && dst[0].buffer && dst[1].buffer && dst[2].buffer);
	assert(tmp && pointer_is_aligned(tmp));

	assert(pointer_is_aligned(src[0].buffer) && pointer_is_aligned(src[1].buffer) && pointer_is_aligned(src[2].buffer));
	assert(pointer_is_aligned(dst[0].buffer) && pointer_is_aligned(dst[1].buffer) && pointer_is_aligned(dst[2].buffer));

	try {
		PlaneDescriptor src_desc[3];
		PlaneDescriptor dst_desc[3];

		ImageTile<const void> src_tiles[3];
		ImageTile<void> dst_tiles[3];

		for (int p = 0; p < 3; ++p) {
			get_image_tile(&src[p], &src_tiles[p], &src_desc[p]);
			get_image_tile(&dst[p], &dst_tiles[p], &dst_desc[p]);
		}

		ctx->p.process_tile(src_tiles, dst_tiles, tmp);
	} catch (const ZimgException &e) {
		handle_exception(e);
		ret = g_last_error;
	}

	return ret;
}

void zimg_colorspace_delete(zimg_colorspace_context *ctx)
{
	delete ctx;
}


struct zimg_depth_context {
	depth::Depth p;
};

zimg_depth_context *zimg_depth_create(int dither_type)
{
	zimg_depth_context *ret = nullptr;

	try {
		ret = new zimg_depth_context{ depth::Depth{ get_dither_type(dither_type), g_cpu_type } };
	} catch (const ZimgException &e) {
		handle_exception(e);
	} catch (const std::bad_alloc &) {
		handle_bad_alloc();
	}

	return ret;
}

int zimg_depth_tile_supported(zimg_depth_context *ctx, int pixel_in, int pixel_out)
{
	int ret = 0;

	assert(ctx);

	try {
		ret = ctx->p.tile_supported(get_pixel_type(pixel_in), get_pixel_type(pixel_out));
	} catch (const ZimgException &e) {
		handle_exception(e);
	}

	return ret;
}

size_t zimg_depth_tmp_size(zimg_depth_context *ctx, int width)
{
	return ctx->p.tmp_size(width);
}

int zimg_depth_process(zimg_depth_context *ctx, const zimg_image_tile_t *src, const zimg_image_tile_t *dst, void *tmp)
{
	int ret = 0;

	assert(ctx);
	assert(src && src->buffer && pointer_is_aligned(src->buffer));
	assert(dst && dst->buffer && pointer_is_aligned(dst->buffer));
	assert(pointer_is_aligned(tmp));

	try {
		PlaneDescriptor src_desc;
		PlaneDescriptor dst_desc;

		ImageTile<const void> src_tile;
		ImageTile<void> dst_tile;

		get_image_tile(src, &src_tile, &src_desc);
		get_image_tile(dst, &dst_tile, &dst_desc);

		if (!ctx->p.tile_supported(src_desc.format.type, dst_desc.format.type)) {
			assert(src->plane_offset_i == 0 && src->plane_offset_j == 0);
			assert(dst->plane_offset_i == 0 && dst->plane_offset_j == 0);
		}

		ctx->p.process_tile(src_tile, dst_tile, tmp);
	} catch (const ZimgException &e) {
		handle_exception(e);
		ret = g_last_error;
	}

	return ret;
}

void zimg_depth_delete(zimg_depth_context *ctx)
{
	delete ctx;
}


struct zimg_resize_context {
	resize::Resize p;
};

int zimg_resize_horizontal_first(double xscale, double yscale)
{
	return resize::resize_horizontal_first(xscale, yscale);
}

zimg_resize_context *zimg_resize_create(int filter_type, int horizontal, int src_dim, int dst_dim,
                                        double shift, double width, double filter_param_a, double filter_param_b)
{
	zimg_resize_context *ret = nullptr;

	try {
		std::unique_ptr<resize::Filter> f{ create_filter(filter_type, filter_param_a, filter_param_b) };
		ret = new zimg_resize_context{ resize::Resize{ *f, !!horizontal, src_dim, dst_dim, shift, width, g_cpu_type } };
	} catch (const ZimgException &e) {
		handle_exception(e);
	} catch (const std::bad_alloc &) {
		handle_bad_alloc();
	}

	return ret;
}

int zimg_resize_pixel_supported(zimg_resize_context *ctx, int pixel_type)
{
	int ret = 0;

	try {
		ret = ctx->p.pixel_supported(get_pixel_type(pixel_type));
	} catch (const ZimgException &e) {
		handle_exception(e);
	}

	return ret;
}

void zimg_resize_dependent_rect(zimg_resize_context *ctx, int dst_top, int dst_left, int dst_bottom, int dst_right,
                                int *src_top, int *src_left, int *src_bottom, int *src_right)
{
	assert(ctx);
	assert(dst_top >= 0 && dst_bottom > dst_top);
	assert(dst_left >= 0 && dst_right > dst_left);
	assert(src_top && src_left && src_bottom && src_right);

	ctx->p.dependent_rect(dst_top, dst_left, dst_bottom, dst_right, src_top, src_left, src_bottom, src_right);
}

int zimg_resize_process_tile(zimg_resize_context *ctx, const zimg_image_tile_t *src, const zimg_image_tile_t *dst)
{
	int ret = 0;

	assert(ctx);
	assert(src && src->buffer);
	assert(dst && dst->buffer && pointer_is_aligned(dst->buffer));
	assert(src->plane_offset_i >= 0 && src->plane_offset_j >= 0);
	assert(dst->plane_offset_i >= 0 && dst->plane_offset_j >= 0);

	try {
		PlaneDescriptor src_desc;
		PlaneDescriptor dst_desc;

		ImageTile<const void> src_tile;
		ImageTile<void> dst_tile;

		int src_top, src_left, src_bottom, src_right;

		get_image_tile(src, &src_tile, &src_desc);
		get_image_tile(dst, &dst_tile, &dst_desc);

		ctx->p.dependent_rect(dst->plane_offset_i, dst->plane_offset_j, dst->plane_offset_i + TILE_HEIGHT, dst->plane_offset_j + TILE_WIDTH,
		                      &src_top, &src_left, &src_bottom, &src_right);
		assert(src->plane_offset_i <= src_top && src->plane_offset_j <= src_left);

		src_tile = src_tile.sub_tile(src_top - src->plane_offset_i, src_left - src->plane_offset_j);
		ctx->p.process(src_tile, dst_tile, dst->plane_offset_i, dst->plane_offset_j);
	} catch (const ZimgException &e) {
		handle_exception(e);
		ret = g_last_error;
	}

	return ret;
}

void zimg_resize_delete(zimg_resize_context *ctx)
{
	delete ctx;
}

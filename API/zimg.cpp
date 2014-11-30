#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include "Common/align.h"
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


struct zimg_colorspace_context {
	colorspace::ColorspaceConversion p;
};

struct zimg_depth_context {
	depth::Depth p;
};

struct zimg_resize_context {
	resize::Resize pass1;
	resize::Resize pass2;
	int src_width;
	int tmp_width;
	int tmp_height;
};


int zimg_check_api_version(int ver)
{
	return ZIMG_API_VERSION >= ver;
}

int zimg_get_last_error(char *err_msg, size_t n)
{
	size_t sz;
	size_t to_copy;

	if (err_msg && n) {
		sz = std::strlen(g_last_error_msg) + 1;
		to_copy = sz > n ? n : sz;

		std::memcpy(err_msg, g_last_error_msg, to_copy);
		err_msg[n - 1] = '\0';
	}

	return g_last_error;
}

void zimg_clear_last_error(void)
{
	std::memset(g_last_error_msg, 0, sizeof(g_last_error_msg));
	g_last_error = 0;
}

void zimg_set_cpu(int cpu)
{
	g_cpu_type = get_cpu_class(cpu);
}


zimg_colorspace_context *zimg_colorspace_create(int matrix_in, int transfer_in, int primaries_in,
                                                int matrix_out, int transfer_out, int primaries_out)
{
	zimg_colorspace_context *ret = nullptr;

	try {
		colorspace::ColorspaceDefinition csp_in;
		colorspace::ColorspaceDefinition csp_out;

		csp_in.matrix = get_matrix_coeffs(matrix_in);
		csp_in.transfer = get_transfer_characteristics(transfer_in);
		csp_in.primaries = get_color_primaries(primaries_in);

		csp_out.matrix = get_matrix_coeffs(matrix_out);
		csp_out.transfer = get_transfer_characteristics(transfer_out);
		csp_out.primaries = get_color_primaries(primaries_out);

		ret = new zimg_colorspace_context{ { csp_in, csp_out, g_cpu_type } };
	} catch (const ZimgException &e) {
		handle_exception(e);
	} catch (const std::bad_alloc &) {
		handle_bad_alloc();
	}

	return ret;
}

size_t zimg_colorspace_tmp_size(zimg_colorspace_context *ctx, int)
{
	return ctx->p.tmp_size(64, 64) * pixel_size(PixelType::FLOAT);
}

int zimg_colorspace_process(zimg_colorspace_context *ctx, const void * const src[3], void * const dst[3], void *tmp,
                            int width, int height, const int src_stride[3], const int dst_stride[3], int pixel_type)
{
	zimg_clear_last_error();

	try {
		PixelType type = get_pixel_type(pixel_type);
		int pxsize = pixel_size(type);

		ImageTile src_tiles[3] = { 0 };
		ImageTile dst_tiles[3] = { 0 };

		if (!ctx->p.pixel_supported(type))
			throw ZimgUnsupportedError{ "unsupported pixel format" };

		for (int p = 0; p < 3; ++p) {
			src_tiles[p].byte_stride = src_stride[p];
			dst_tiles[p].byte_stride = dst_stride[p];

			src_tiles[p].format = default_pixel_format(type);
			dst_tiles[p].format = default_pixel_format(type);
		}

		for (int i = 0; i < height; i += 64) {
			for (int j = 0; j < width; j += 64) {
				int tile_h = std::min(height - i, 64);
				int tile_w = std::min(width - j, 64);

				for (int p = 0; p < 3; ++p) {
					src_tiles[p].ptr = (char *)src[p] + i * dst_stride[p] + j * pxsize;
					dst_tiles[p].ptr = (char *)dst[p] + i * dst_stride[p] + j * pxsize;

					src_tiles[p].width = tile_w;
					src_tiles[p].height = tile_h;

					dst_tiles[p].width = tile_w;
					dst_tiles[p].height = tile_h;
				}

				ctx->p.process_tile(src_tiles, dst_tiles, tmp);
			}
		}
	} catch (const ZimgException &e) {
		handle_exception(e);
	}

	return g_last_error;
}

void zimg_colorspace_delete(zimg_colorspace_context *ctx)
{
	delete ctx;
}


zimg_depth_context *zimg_depth_create(int dither_type)
{
	zimg_depth_context *ret = nullptr;

	try {
		depth::DitherType dither = get_dither_type(dither_type);
		ret = new zimg_depth_context{ { dither, g_cpu_type } };
	} catch (const ZimgException &e) {
		handle_exception(e);
	} catch (const std::bad_alloc &) {
		handle_bad_alloc();
	}

	return ret;
}

size_t zimg_depth_tmp_size(zimg_depth_context *ctx, int width)
{
	return ctx->p.tmp_size(width) * pixel_size(PixelType::FLOAT);
}

int zimg_depth_process(zimg_depth_context *ctx, const void *src, void *dst, void *tmp,
                       int width, int height, int src_stride, int dst_stride,
					   int pixel_in, int pixel_out, int depth_in, int depth_out, int fullrange_in, int fullrange_out, int chroma)
{
	zimg_clear_last_error();

	try {
		PixelFormat src_format{ get_pixel_type(pixel_in), depth_in, !!fullrange_in, !!chroma };
		PixelFormat dst_format{ get_pixel_type(pixel_out), depth_out, !!fullrange_out, !!chroma };

		ImageTile src_tile{ (void *)src, src_stride, width, height, src_format };
		ImageTile dst_tile{ dst, dst_stride, width, height, dst_format };

		ctx->p.process_tile(src_tile, dst_tile, tmp);
	} catch (const ZimgException &e) {
		handle_exception(e);
	}

	return g_last_error;
}

void zimg_depth_delete(zimg_depth_context *ctx)
{
	delete ctx;
}


zimg_resize_context *zimg_resize_create(int filter_type, int src_width, int src_height, int dst_width, int dst_height,
                                        double shift_w, double shift_h, double subwidth, double subheight,
                                        double filter_param_a, double filter_param_b)
{
	zimg_resize_context *ret = nullptr;

	try {
		std::unique_ptr<resize::Filter> filter{ create_filter(filter_type, filter_param_a, filter_param_b) };
		resize::Resize resize_h{ *filter, true, src_width, dst_width, shift_w, subwidth, g_cpu_type };
		resize::Resize resize_v{ *filter, false, src_height, dst_height, shift_h, subheight, g_cpu_type };

		bool horizontal_first = resize::resize_horizontal_first((double)dst_width / src_width, (double)dst_height / src_height);

		const resize::Resize &pass1 = horizontal_first ? resize_h : resize_v;
		const resize::Resize &pass2 = horizontal_first ? resize_v : resize_h;
		int tmp_width = horizontal_first ? dst_width : src_width;
		int tmp_height = horizontal_first ? src_height : dst_height;

		ret = new zimg_resize_context{ pass1, pass2, src_width, tmp_width, tmp_height };
	} catch (const ZimgException &e) {
		handle_exception(e);
	} catch (const std::bad_alloc &) {
		handle_bad_alloc();
	}

	return ret;
}

size_t zimg_resize_tmp_size(zimg_resize_context *ctx, int pixel_type)
{
	size_t ret = 0;

	try {
		PixelType type = get_pixel_type(pixel_type);

		ret += ctx->pass1.tmp_size(type, ctx->src_width) * pixel_size(type);
		ret += ctx->pass2.tmp_size(type, ctx->tmp_width) * pixel_size(type);

		// Temporary frame.
		ret += (size_t)align(ctx->tmp_width * pixel_size(type), ALIGNMENT) * ctx->tmp_height;
	} catch (const ZimgException &e) {
		handle_exception(e);
	}

	return ret;
}

int zimg_resize_process(zimg_resize_context *ctx, const void *src, void *dst, void *tmp,
                        int src_width, int src_height, int dst_width, int dst_height,
                        int src_stride, int dst_stride, int pixel_type)
{
	zimg_clear_last_error();

	try {
		PixelType type = get_pixel_type(pixel_type);
		int tmp_stride = align(ctx->tmp_width * pixel_size(type), ALIGNMENT);

		ImageTile src_tile{ (void *)src, src_stride, src_width, src_height, default_pixel_format(type) };
		ImageTile dst_tile{ (void *)dst, dst_stride, dst_width, dst_height, default_pixel_format(type) };
		ImageTile tmp_tile{ tmp, tmp_stride, ctx->tmp_width, ctx->tmp_height, default_pixel_format(type) };

		tmp = (char *)tmp + (size_t)tmp_stride * ctx->tmp_height;

		ctx->pass1.process(src_tile, tmp_tile, tmp);
		ctx->pass2.process(tmp_tile, dst_tile, tmp);
	} catch (const ZimgException &e) {
		handle_exception(e);
	}

	return g_last_error;
}

void zimg_resize_delete(zimg_resize_context *ctx)
{
	delete ctx;
}

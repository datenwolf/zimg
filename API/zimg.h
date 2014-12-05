#ifndef ZIMG_H_
#define ZIMG_H_

#ifdef __cplusplus
extern "C" {;
#endif

#include <stddef.h>

#define ZIMG_API_VERSION 2

#define ZIMG_ERROR_UNKNOWN           -1
#define ZIMG_ERROR_LOGIC            100 /* Internal logic error. */
#define ZIMG_ERROR_OUT_OF_MEMORY    200 /* Error allocating internal structures. */
#define ZIMG_ERROR_ILLEGAL_ARGUMENT 300 /* Illegal value provided for argument. */
#define ZIMG_ERROR_UNSUPPORTED      400 /* Operation not supported. */

/**
 * Return the last error code. Error information is thread-local.
 * A descriptive error message is placed in the [n]-byte buffer located at [err_msg].
 */
int zimg_get_last_error(char *err_msg, size_t n);

/* Set the last error to 0 and clear the stored error message. */
void zimg_clear_last_error(void);


#define ZIMG_CPU_NONE 0
#define ZIMG_CPU_AUTO 1

#if defined(__i386) || defined(_M_IX86) || defined(_M_X64) || defined(__x86_64__)
  #define ZIMG_CPU_X86_MMX   1000
  #define ZIMG_CPU_X86_SSE   1001
  #define ZIMG_CPU_X86_SSE2  1002
  #define ZIMG_CPU_X86_SSE3  1003
  #define ZIMG_CPU_X86_SSSE3 1004
  #define ZIMG_CPU_X86_SSE41 1005
  #define ZIMG_CPU_X86_SSE42 1006
  #define ZIMG_CPU_X86_AVX   1007
  #define ZIMG_CPU_X86_F16C  1008
  #define ZIMG_CPU_X86_AVX2  1009
#endif

/**
 * Set the desired CPU type to [cpu]. The result is set globally.
 * This function is thread-safe.
 */
void zimg_set_cpu(int cpu);


#define ZIMG_PIXEL_BYTE  0 /* Unsigned integer, one byte per sample. */
#define ZIMG_PIXEL_WORD  1 /* Unsigned integer, two bytes per sample. */
#define ZIMG_PIXEL_HALF  2 /* IEEE-756 half precision (binary16). */
#define ZIMG_PIXEL_FLOAT 3 /* IEEE-756 single precision (binary32). */


/* Many functions operate on subregions (tiles) of an input plane. */
#define ZIMG_TILE_WIDTH  64
#define ZIMG_TILE_HEIGHT 64

/**
 * Descriptor struct used to represent image tiles.
 * Not all fields are required by all functions.
 */
typedef struct zimg_image_tile_t {
	void *buffer;       /* Pointer to top-left pixel of tile. */
	int stride;         /* Distance between scanlines in bytes. Must be positive. */
	int pixel_type;     /* Pixel type contained in tile. */

	/**
	 * The remaining fields are only required for certain functions.
	 */
	int plane_offset_i; /* Row index of tile in containing plane. */
	int plane_offset_j; /* Column index of tile in containing plane. */
	int plane_width;    /* Width of plane containing the tile. */
	int plane_height;   /* Height of plane containing the tile. */
	int depth;          /* For BYTE and WORD, the active bit depth. */
	int range;          /* 0 for limited range and 1 for full range. */
	int chroma;         /* 0 for luma or RGB and 1 for Cb/Cr. */
} zimg_image_tile_t;


/* Chosen to match ITU-T H.264 and H.265 */
#define ZIMG_MATRIX_RGB        0
#define ZIMG_MATRIX_709        1
#define ZIMG_MATRIX_470BG      5
#define ZIMG_MATRIX_170M       6 /* Equivalent to 5. */
#define ZIMG_MATRIX_2020_NCL   9
#define ZIMG_MATRIX_2020_CL   10

#define ZIMG_TRANSFER_709      1
#define ZIMG_TRANSFER_601      6 /* Equivalent to 1. */
#define ZIMG_TRANSFER_LINEAR   8
#define ZIMG_TRANSFER_2020_10 14 /* The Rec.709 curve is used for both 2020 10-bit and 12-bit. */
#define ZIMG_TRANSFER_2020_12 15

#define ZIMG_PRIMARIES_709     1
#define ZIMG_PRIMARIES_170M    6
#define ZIMG_PRIMARIES_240M    7 /* Equivalent to 6. */
#define ZIMG_PRIMARIES_2020    9

typedef struct zimg_colorspace_context zimg_colorspace_context;

/**
 * Create a context to convert between the described colorspaces.
 * On error, a NULL pointer is returned.
 */
zimg_colorspace_context *zimg_colorspace_create(int matrix_in, int transfer_in, int primaries_in,
                                                int matrix_out, int transfer_out, int primaries_out);

/* Get the temporary buffer size in bytes required to process a tile using [ctx]. */
size_t zimg_colorspace_tmp_size(zimg_colorspace_context *ctx);

/* Check if the context [ctx] supports processing [pixel_type]. */
int zimg_colorspace_pixel_supported(zimg_colorspace_context *ctx, int pixel_type);

/**
 * Process a tile. The channel order must be either R-G-B or Y-Cb-Cr, depending on the colorspace.
 * The input and output tiles may point to the same buffer.
 * On success, 0 is returned, else a corresponding error code.
 */
int zimg_colorspace_process_tile(zimg_colorspace_context *ctx, const zimg_image_tile_t src[3], const zimg_image_tile_t dst[3], void *tmp, int pixel_type);

/* Delete the context. */
void zimg_colorspace_delete(zimg_colorspace_context *ctx);


#define ZIMG_DITHER_NONE            0
#define ZIMG_DITHER_ORDERED         1
#define ZIMG_DITHER_RANDOM          2
#define ZIMG_DITHER_ERROR_DIFFUSION 3

typedef struct zimg_depth_context zimg_depth_context;

/**
 * Create a context to convert between pixel formats using the given [dither_type].
 * On error, a NULL pointer is returned.
 */
zimg_depth_context *zimg_depth_create(int dither_type);

/**
 * Check if the context [ctx] operates on tiles or planes when converting [pixel_in] to [pixel_out].
 *
 * If this function returns zero, a tile containing an entire plane must be passed to zimg_depth_process.
 * If this function returns non-zero, a 64x64 tile must be passed to zimg_depth_process.
 */
int zimg_depth_tile_supported(zimg_depth_context *ctx, int pixel_in, int pixel_out);

/** 
 * Get the temporary buffer size in bytes required to process a plane with [width] using [ctx].
 * This function is only required if zimg_depth_tile_supported returns zero.
 */
size_t zimg_depth_tmp_size(zimg_depth_context *ctx, int width);

/**
 * Process a tile or a plane (see: zimg_depth_tile_supported).
 * If tiles are supported, a 64x64 tile is processed. Otherwise, an entire plane is processed.
 *
 * The tiles must have the depth, range, and chroma fields set.
 * When processing planes, the plane_width and plane_height fields must also be set.
 *
 * On success, 0 is returned, else a corresponding error code.
 */
int zimg_depth_process(zimg_depth_context *ctx, const zimg_image_tile_t *src, const zimg_image_tile_t *dst, void *tmp);

/* Delete the context. */
void zimg_depth_delete(zimg_depth_context *ctx);


#define ZIMG_RESIZE_POINT    0
#define ZIMG_RESIZE_BILINEAR 1
#define ZIMG_RESIZE_BICUBIC  2
#define ZIMG_RESIZE_SPLINE16 3
#define ZIMG_RESIZE_SPLINE36 4
#define ZIMG_RESIZE_LANCZOS  5

typedef struct zimg_resize_context zimg_resize_context;

/**
 * Query whether performing horizontal or vertical resampling first is faster.
 * [xscale] is the horizontal resampling ratio and [yscale] is the vertical resampling ratio.
 *
 * Returns non-zero if horizontal followed by vertical resampling is faster.
 */
int zimg_resize_horizontal_first(double xscale, double yscale);

/**
 * Create a context to apply the given resampling ratio.
 * The filter maps the input range [shift, shift + width) to the output range [0, dst_dim).
 * The resampling is done horizontally if the [horizontal] argument is non-zero.
 *
 * The meaning of [filter_param_a] and [filter_param_b] depend on the selected filter.
 * Passing NAN for either filter parameter results in a default value being used.
 * For lanczos, "a" is the number of taps, and for bicubic, they are the "b" and "c" parameters.
 *
 * On error, a NULL pointer is returned.
 */
zimg_resize_context *zimg_resize_create(int filter_type, int horizontal, int src_dim, int dst_dim,
                                        double shift, double width, double filter_param_a, double filter_param_b);

/* Check if the context [ctx] supports processing [pixel_type]. */
int zimg_resize_pixel_supported(zimg_resize_context *ctx, int pixel_type);

/**
 * Get the input rectangle required to process an output rectangle.
 * The rectangle required to process the rectangle [dst_top], [dst_left], [dst_bottom], [dst_right]
 * is returned in the pointers [src_top], [src_left], [src_bottom], and [src_right].
 */
void zimg_resize_dependent_rect(zimg_resize_context *ctx, int dst_top, int dst_left, int dst_bottom, int dst_right,
                                int *src_top, int *src_left, int *src_bottom, int *src_right);

/**
 * Process a 64x64 tile. The input tile must contain the rectangle indicated by zimg_resize_dependent_rect.
 * This function may read up to 16 pixels past the end of the input tile, which must be padded accordingly.
 *
 * The tiles must have the plane_offset_i and plane_offset_j fields set.
 *
 * On success, 0 is returned, else a corresponding error code.
 */
int zimg_resize_process_tile(zimg_resize_context *ctx, const zimg_image_tile_t *src, const zimg_image_tile_t *dst);

/* Delete the context. */
void zimg_resize_delete(zimg_resize_context *ctx);



/**
 * The inline functions below are convenience wrappers that process entire planes.
 * These functions are not part of the API.
 */
#ifdef ZIMG_PLANE_HELPER

#if defined(__cplusplus) || (defined(__STDC_VERSION__ ) && __STDC_VERSION__ >= 199901L)
  #define ZIMG_INLINE inline
#elif defined (_GNUC)
  #define ZIMG_INLINE __inline__
#elif defined (_MSC_VER)
  #define ZIMG_INLINE __inline
#else
  #define ZIMG_INLINE
#endif

#define ZIMG_MAX(a, b) ((a) > (b) ? (a) : (b))
#define ZIMG_MIN(a, b) ((a) < (b) ? (a) : (b))

static ZIMG_INLINE int _zimg_pixel_size(int pixel_type)
{
	switch (pixel_type) {
	case ZIMG_PIXEL_BYTE:
		return 1;
	case ZIMG_PIXEL_WORD:
	case ZIMG_PIXEL_HALF:
		return 2;
	case ZIMG_PIXEL_FLOAT:
		return 4;
	default:
		return 0;
	}
}

static ZIMG_INLINE size_t _zimg_tile_size(int pixel_type)
{
	return (size_t)ZIMG_TILE_WIDTH * ZIMG_TILE_HEIGHT * _zimg_pixel_size(pixel_type);
}

static ZIMG_INLINE void _zimg_bit_blt(const void *src, void *dst, int line_size, int height, int src_stride, int dst_stride)
{
	const char *src_b = (const char *)src;
	char *dst_b = (char *)dst;
	int i, j;

	for (i = 0; i < height; ++i) {
		for (j = 0; j < line_size; ++j) {
			dst_b[i * dst_stride + j] = src_b[i * src_stride + j];
		}
	}
}

static ZIMG_INLINE size_t _zimg_colorspace_plane_tmp_size(zimg_colorspace_context *ctx, int pixel_type)
{
	return zimg_colorspace_tmp_size(ctx) * sizeof(float) + 3 * _zimg_tile_size(pixel_type);
}

static ZIMG_INLINE void _zimg_colorspace_plane_process(zimg_colorspace_context *ctx, const void * const src[3], void * const dst[3], void *tmp,
                                                      int width, int height, const int src_stride[3], const int dst_stride[3], int pixel_type)
{
	zimg_image_tile_t src_tiles[3];
	zimg_image_tile_t dst_tiles[3];

	int pixel_size = _zimg_pixel_size(pixel_type);
	int tmp_stride = ZIMG_TILE_WIDTH * pixel_size;
	size_t tmp_size = _zimg_tile_size(pixel_type);

	int i, j;

	src_tiles[0].pixel_type = dst_tiles[0].pixel_type = pixel_type;
	src_tiles[1].pixel_type = dst_tiles[1].pixel_type = pixel_type;
	src_tiles[2].pixel_type = dst_tiles[2].pixel_type = pixel_type;

	for (i = 0; i < height; i += ZIMG_TILE_HEIGHT) {
		for (j = 0; j < width; j += ZIMG_TILE_WIDTH) {
			int need_copy = i + ZIMG_TILE_HEIGHT > height || j + ZIMG_TILE_WIDTH > width;

			void *src_ptr[3];
			void *dst_ptr[3];

			src_ptr[0] = (char *)src[0] + i * src_stride[0] + j * pixel_size;
			src_ptr[1] = (char *)src[1] + i * src_stride[1] + j * pixel_size;
			src_ptr[2] = (char *)src[2] + i * src_stride[2] + j * pixel_size;

			dst_ptr[0] = (char *)dst[0] + i * dst_stride[0] + j * pixel_size;
			dst_ptr[1] = (char *)dst[1] + i * dst_stride[1] + j * pixel_size;
			dst_ptr[2] = (char *)dst[2] + i * dst_stride[2] + j * pixel_size;

			if (need_copy) {
				int tile_width  = ZIMG_MIN(width - j, ZIMG_TILE_WIDTH);
				int tile_height = ZIMG_MIN(height - i, ZIMG_TILE_HEIGHT);
				void *ttmp;

				src_tiles[0].buffer = (char *)tmp + 0 * tmp_size;
				src_tiles[1].buffer = (char *)tmp + 1 * tmp_size;
				src_tiles[2].buffer = (char *)tmp + 2 * tmp_size;
				ttmp = (char *)tmp + 3 * tmp_size;

				src_tiles[0].stride = tmp_stride;
				src_tiles[1].stride = tmp_stride;
				src_tiles[2].stride = tmp_stride;

				_zimg_bit_blt(src_ptr[0], src_tiles[0].buffer, tile_width * pixel_size, tile_height, src_stride[0], tmp_stride);
				_zimg_bit_blt(src_ptr[1], src_tiles[1].buffer, tile_width * pixel_size, tile_height, src_stride[1], tmp_stride);
				_zimg_bit_blt(src_ptr[2], src_tiles[2].buffer, tile_width * pixel_size, tile_height, src_stride[2], tmp_stride);

				zimg_colorspace_process_tile(ctx, src_tiles, src_tiles, ttmp, pixel_type);

				_zimg_bit_blt(src_tiles[0].buffer, dst_ptr[0], tile_width * pixel_size, tile_height, tmp_stride, dst_stride[0]);
				_zimg_bit_blt(src_tiles[1].buffer, dst_ptr[1], tile_width * pixel_size, tile_height, tmp_stride, dst_stride[1]);
				_zimg_bit_blt(src_tiles[2].buffer, dst_ptr[2], tile_width * pixel_size, tile_height, tmp_stride, dst_stride[2]);
			} else {
				src_tiles[0].buffer = src_ptr[0];
				src_tiles[1].buffer = src_ptr[1];
				src_tiles[2].buffer = src_ptr[2];

				dst_tiles[0].buffer = dst_ptr[0];
				dst_tiles[1].buffer = dst_ptr[1];
				dst_tiles[2].buffer = dst_ptr[2];

				src_tiles[0].stride = src_stride[0];
				src_tiles[1].stride = src_stride[1];
				src_tiles[2].stride = src_stride[2];

				dst_tiles[0].stride = dst_stride[0];
				dst_tiles[1].stride = dst_stride[1];
				dst_tiles[2].stride = dst_stride[2];

				zimg_colorspace_process_tile(ctx, src_tiles, dst_tiles, tmp, pixel_type);
			}
		}
	}
}

static ZIMG_INLINE size_t _zimg_depth_plane_tmp_size(zimg_depth_context *ctx, int width, int pixel_in, int pixel_out)
{
	size_t ret = zimg_depth_tmp_size(ctx, width);

	if (zimg_depth_tile_supported(ctx, pixel_in, pixel_out))
		ret += _zimg_tile_size(pixel_in) + _zimg_tile_size(pixel_out);

	return ret;
}

static ZIMG_INLINE void _zimg_depth_plane_process(zimg_depth_context *ctx, const void *src, void *dst, void *tmp,
                                                    int width, int height, int src_stride, int dst_stride,
                                                    int pixel_in, int pixel_out, int depth_in, int depth_out, int range_in, int range_out, int chroma)
{
	zimg_image_tile_t src_tile;
	zimg_image_tile_t dst_tile;

	src_tile.pixel_type = pixel_in;
	src_tile.depth = depth_in;
	src_tile.range = range_in;
	src_tile.chroma = chroma;

	dst_tile.pixel_type = pixel_out;
	dst_tile.depth = depth_out;
	dst_tile.range = range_out;
	dst_tile.chroma = chroma;

	if (zimg_depth_tile_supported(ctx, pixel_in, pixel_out)) {
		int pixel_size_in = _zimg_pixel_size(pixel_in);
		int pixel_size_out = _zimg_pixel_size(pixel_out);

		int tmp_stride_in = ZIMG_TILE_WIDTH * pixel_size_in;
		int tmp_stride_out = ZIMG_TILE_WIDTH * pixel_size_out;

		size_t tmp_size_in = _zimg_tile_size(pixel_in);
		size_t tmp_size_out = _zimg_tile_size(pixel_out);

		int i, j;

		for (i = 0; i < height; i += ZIMG_TILE_HEIGHT) {
			for (j = 0; j < width; j += ZIMG_TILE_WIDTH) {
				int need_copy = i + ZIMG_TILE_HEIGHT > height || j + ZIMG_TILE_WIDTH > width;
				void *src_ptr = (char *)src + i * src_stride + j * pixel_size_in;
				void *dst_ptr = (char *)dst + i * dst_stride + j * pixel_size_out;

				if (need_copy) {
					int tile_width = ZIMG_MIN(width - j, ZIMG_TILE_WIDTH);
					int tile_height = ZIMG_MIN(height - i, ZIMG_TILE_HEIGHT);
					void *tmp2;

					src_tile.buffer = tmp;
					dst_tile.buffer = (char *)src_tile.buffer + tmp_size_in;
					tmp2 = (char *)dst_tile.buffer + tmp_size_out;

					src_tile.stride = tmp_stride_in;
					dst_tile.stride = tmp_stride_out;

					_zimg_bit_blt(src_ptr, src_tile.buffer, tile_width * pixel_size_in, tile_height, src_stride, tmp_stride_in);

					zimg_depth_process(ctx, &src_tile, &dst_tile, tmp2);

					_zimg_bit_blt(dst_tile.buffer, dst_ptr, tile_width * pixel_size_out, tile_height, tmp_stride_out, dst_stride);
				} else {
					src_tile.buffer = src_ptr;
					dst_tile.buffer = dst_ptr;

					src_tile.stride = src_stride;
					dst_tile.stride = dst_stride;

					zimg_depth_process(ctx, &src_tile, &dst_tile, tmp);
				}
			}
		}
	} else {
		src_tile.buffer = (void *)src;
		dst_tile.buffer = dst;

		src_tile.plane_width = dst_tile.plane_width = width;
		src_tile.plane_height = dst_tile.plane_height = height;

		zimg_depth_process(ctx, &src_tile, &dst_tile, tmp);
	}
}

static ZIMG_INLINE size_t _zimg_resize_plane_tmp_size(zimg_resize_context *ctx, int src_width, int src_height, int dst_width, int dst_height, int pixel_type)
{
	size_t sz = 0;
	int pixel_size = _zimg_pixel_size(pixel_type);

	int top, left, bottom, right;
	int i, j;

	for (i = 0; i < dst_height; i += ZIMG_TILE_HEIGHT) {
		for (j = 0; j < dst_width; j += ZIMG_TILE_WIDTH) {
			zimg_resize_dependent_rect(ctx, i, j, i + ZIMG_TILE_HEIGHT, j + ZIMG_TILE_WIDTH, &top, &left, &bottom, &right);

			if (bottom + ZIMG_TILE_HEIGHT > src_height || right + ZIMG_TILE_WIDTH > src_width) {
				size_t stride = (right - left + ZIMG_TILE_WIDTH) * pixel_size;

				if (stride % ZIMG_TILE_WIDTH)
					stride += ZIMG_TILE_WIDTH - stride % ZIMG_TILE_WIDTH;

				sz = ZIMG_MAX(sz, stride * (bottom - top));
			}
		}
	}

	sz += _zimg_tile_size(pixel_type);

	return sz;
}

static ZIMG_INLINE void _zimg_resize_plane_process(zimg_resize_context *ctx, const void *src, void *dst, void *tmp,
                                                   int src_width, int src_height, int dst_width, int dst_height, int src_stride, int dst_stride, int pixel_type)
{
	zimg_image_tile_t src_tile;
	zimg_image_tile_t dst_tile;
		
	int pixel_size = _zimg_pixel_size(pixel_type);
	int top, left, bottom, right;
	int i, j;

	src_tile.pixel_type = dst_tile.pixel_type = pixel_type;

	for (i = 0; i < dst_height; i += ZIMG_TILE_HEIGHT) {
		for (j = 0; j < dst_width; j += ZIMG_TILE_WIDTH) {
			int need_copy_src;
			int need_copy_dst;

			void *src_ptr;
			void *dst_ptr;
			void *ttmp;

			zimg_resize_dependent_rect(ctx, i, j, i + ZIMG_TILE_HEIGHT, j + ZIMG_TILE_WIDTH, &top, &left, &bottom, &right);

			need_copy_src = bottom > src_height || right + ZIMG_TILE_WIDTH > src_width;
			need_copy_dst = i + ZIMG_TILE_HEIGHT > dst_height || j + ZIMG_TILE_WIDTH > dst_width;

			src_ptr = (char *)src + top * src_stride + left * pixel_size;
			dst_ptr = (char *)dst + i * dst_stride + j * pixel_size;

			src_tile.plane_offset_i = top;
			src_tile.plane_offset_j = left;
			dst_tile.plane_offset_i = i;
			dst_tile.plane_offset_j = j;

			ttmp = tmp;

			if (need_copy_src) {
				int tile_width = ZIMG_MIN(right - left, src_width - left);
				int tile_height = ZIMG_MIN(bottom - top, src_height - top);

				src_tile.buffer = ttmp;
				src_tile.stride = (right - left + ZIMG_TILE_WIDTH) * pixel_size;

				if (src_tile.stride % ZIMG_TILE_WIDTH)
					src_tile.stride += ZIMG_TILE_WIDTH - src_tile.stride % ZIMG_TILE_WIDTH;

				_zimg_bit_blt(src_ptr, src_tile.buffer, tile_width * pixel_size, tile_height, src_stride, src_tile.stride);

				ttmp = (char *)ttmp + src_tile.stride * (bottom - top);
			} else {
				src_tile.buffer = src_ptr;
				src_tile.stride = src_stride;
			}

			if (need_copy_dst) {
				int tile_width = ZIMG_MIN(dst_width - j, ZIMG_TILE_WIDTH);
				int tile_height = ZIMG_MIN(dst_height - i, ZIMG_TILE_HEIGHT);

				dst_tile.buffer = ttmp;
				dst_tile.stride = ZIMG_TILE_WIDTH * pixel_size;

				zimg_resize_process_tile(ctx, &src_tile, &dst_tile);

				_zimg_bit_blt(dst_tile.buffer, dst_ptr, tile_width * pixel_size, tile_height, dst_tile.stride, dst_stride);
			} else {
				dst_tile.buffer = dst_ptr;
				dst_tile.stride = dst_stride;

				zimg_resize_process_tile(ctx, &src_tile, &dst_tile);
			}
		}
	}
}

#undef ZIMG_MAX
#undef ZIMG_MIN

#undef ZIMG_INLINE
#endif /* ZIMG_PLANE_HELPER */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ZIMG_H_ */

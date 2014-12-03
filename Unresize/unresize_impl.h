#pragma once

#ifndef ZIMG_UNRESIZE_UNRESIZE_IMPL_H_
#define ZIMG_UNRESIZE_UNRESIZE_IMPL_H_

#include "Common/osdep.h"
#include "bilinear.h"

namespace zimg {;

enum class CPUClass;

template <class T>
class ImageTile;

namespace unresize {;

struct ScalarPolicy_F32 {
	typedef float data_type;

	FORCE_INLINE float load(const float *src) { return *src; }

	FORCE_INLINE void store(float *dst, float x) { *dst = x; }
};

template <class T, class Policy>
inline FORCE_INLINE void filter_scanline_h_forward(const BilinearContext &ctx, const ImageTile<const T> &src, T * RESTRICT tmp,
												   int i, int j_begin, int j_end, Policy policy)
{
	const float *c = ctx.lu_c.data();
	const float *l = ctx.lu_l.data();

	float z = j_begin ? policy.load(&src[i][j_begin - 1]) : 0;

	// Matrix-vector product, and forward substitution loop.
	for (int j = j_begin; j < j_end; ++j) {
		const float *row = ctx.matrix_coefficients.data() + j * ctx.matrix_row_stride;
		int left = ctx.matrix_row_offsets[j];

		float accum = 0;
		for (int k = 0; k < ctx.matrix_row_size; ++k) {
			float coeff = row[k];
			float x = policy.load(&src[i][left + k]);
			accum += coeff * x;
		}

		z = (accum - c[j] * z) * l[j];
		policy.store(&tmp[j], z);
	}
}

template <class T, class Policy>
inline FORCE_INLINE void filter_scanline_h_back(const BilinearContext &ctx, const T * RESTRICT tmp, const ImageTile<T> &dst,
												int i, int j_begin, int j_end, Policy policy)
{
	const float *u = ctx.lu_u.data();

	int dst_width = dst.descriptor()->width;
	float w = j_begin < dst_width ? policy.load(&dst[i][j_begin]) : 0;

	// Backward substitution.
	for (int j = j_begin; j > j_end; --j) {
		w = policy.load(&tmp[j - 1]) - u[j - 1] * w;
		policy.store(&dst[i][j - 1], w);
	}
}

template <class T, class Policy>
inline FORCE_INLINE void filter_scanline_v_forward(const BilinearContext &ctx, const ImageTile<const T> &src, const ImageTile<T> &dst,
												   int i, int j_begin, int j_end, Policy policy)
{
	const float *c = ctx.lu_c.data();
	const float *l = ctx.lu_l.data();

	const float *row = ctx.matrix_coefficients.data() + i * ctx.matrix_row_stride;
	int top = ctx.matrix_row_offsets[i];

	for (int j = j_begin; j < j_end; ++j) {
		float z = i ? policy.load(&dst[i - 1][j]) : 0;

		float accum = 0;
		for (int k = 0; k < ctx.matrix_row_size; ++k) {
			float coeff = row[k];
			float x = policy.load(&src[top + k][j]);
			accum += coeff * x;
		}

		z = (accum - c[i] * z) * l[i];
		policy.store(&dst[i][j], z);
	}
}

template <class T, class Policy>
inline FORCE_INLINE void filter_scanline_v_back(const BilinearContext &ctx, const ImageTile<T> &dst, int i, int j_begin, int j_end, Policy policy)
{
	const float *u = ctx.lu_u.data();
	int dst_height = dst.descriptor()->height;

	for (ptrdiff_t j = j_begin; j < j_end; ++j) {
		float w = i < dst_height ? policy.load(&dst[i][j]) : 0;

		w = policy.load(&dst[i - 1][j]) - u[i - 1] * w;
		policy.store(&dst[i - 1][j], w);
	}
}


/**
 * Base class for implementations of unresizing filter.
 */
class UnresizeImpl {
protected:
	/**
	 * Filter coefficients.
	 */
	BilinearContext m_context;

	/**
	 * Initialize the implementation with the given coefficients.
	 *
	 * @param context coefficients
	 */
	UnresizeImpl(const BilinearContext &context);
public:
	/**
	 * Destroy implementation
	 */
	virtual ~UnresizeImpl() = 0;

	virtual void process_f16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, void *tmp) const = 0;

	virtual void process_f32(const ImageTile<const float> &src, const ImageTile<float> &dst, void *tmp) const = 0;
};

/**
 * Create and allocate a execution kernel.
 *
 * @see Unresize::Unresize
 */
UnresizeImpl *create_unresize_impl(bool horizontal, int src_dim, int dst_dim, double shift, CPUClass cpu);

} // namespace unresize
} // namespace zimg

#endif // ZIMG_UNRESIZE_UNRESIZE_IMPL_H_

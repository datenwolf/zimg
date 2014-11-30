#pragma once

#ifndef ZIMG_UNRESIZE_UNRESIZE_IMPL_H_
#define ZIMG_UNRESIZE_UNRESIZE_IMPL_H_

#include <cstdint>
#include "Common/osdep.h"
#include "bilinear.h"

namespace zimg {;

enum class CPUClass;

struct ImageTile;

namespace unresize {;

struct ScalarPolicy_F32 {
	typedef float data_type;

	FORCE_INLINE float load(const float *src) { return *src; }

	FORCE_INLINE void store(float *dst, float x) { *dst = x; }
};

template <class Policy>
inline FORCE_INLINE void filter_scanline_h_forward(const BilinearContext &ctx, const ImageTile &src, typename Policy::data_type * RESTRICT tmp,
												   int i, int j_begin, int j_end, Policy policy)
{
	typedef Policy::data_type data_type;

	TileView<const data_type> src_view{ src };

	const float *c = ctx.lu_c.data();
	const float *l = ctx.lu_l.data();

	float z = j_begin ? policy.load(&src_view[i][j_begin - 1]) : 0;

	// Matrix-vector product, and forward substitution loop.
	for (int j = j_begin; j < j_end; ++j) {
		const float *row = ctx.matrix_coefficients.data() + j * ctx.matrix_row_stride;
		int left = ctx.matrix_row_offsets[j];

		float accum = 0;
		for (int k = 0; k < ctx.matrix_row_size; ++k) {
			float coeff = row[k];
			float x = policy.load(&src_view[i][left + k]);
			accum += coeff * x;
		}

		z = (accum - c[j] * z) * l[j];
		policy.store(&tmp[j], z);
	}
}

template <class Policy>
inline FORCE_INLINE void filter_scanline_h_back(const BilinearContext &ctx, const typename Policy::data_type * RESTRICT tmp, const ImageTile &dst,
												int i, int j_begin, int j_end, Policy policy)
{
	typedef Policy::data_type data_type;

	TileView<data_type> dst_view{ dst };

	const float *u = ctx.lu_u.data();
	float w = j_begin < ctx.dst_width ? policy.load(&dst_view[i][j_begin]) : 0;

	// Backward substitution.
	for (int j = j_begin; j > j_end; --j) {
		w = policy.load(&tmp[j - 1]) - u[j - 1] * w;
		policy.store(&dst_view[i][j - 1], w);
	}
}

template <class Policy>
inline FORCE_INLINE void filter_scanline_v_forward(const BilinearContext &ctx, const ImageTile &src, const ImageTile &dst,
												   int i, int j_begin, int j_end, Policy policy)
{
	typedef Policy::data_type data_type;

	TileView<const data_type> src_view{ src };
	TileView<data_type> dst_view{ dst };

	const float *c = ctx.lu_c.data();
	const float *l = ctx.lu_l.data();

	const float *row = ctx.matrix_coefficients.data() + i * ctx.matrix_row_stride;
	int top = ctx.matrix_row_offsets[i];

	for (int j = j_begin; j < j_end; ++j) {
		float z = i ? policy.load(&dst_view[i - 1][j]) : 0;

		float accum = 0;
		for (int k = 0; k < ctx.matrix_row_size; ++k) {
			float coeff = row[k];
			float x = policy.load(&src_view[top + k][j]);
			accum += coeff * x;
		}

		z = (accum - c[i] * z) * l[i];
		policy.store(&dst_view[i][j], z);
	}
}

template <class Policy>
inline FORCE_INLINE void filter_scanline_v_back(const BilinearContext &ctx, const ImageTile &dst, int i, int j_begin, int j_end, Policy policy)
{
	typedef Policy::data_type data_type;

	TileView<data_type> dst_view{ dst };

	const float *u = ctx.lu_u.data();

	for (ptrdiff_t j = j_begin; j < j_end; ++j) {
		float w = i < ctx.dst_width ? policy.load(&dst_view[i][j]) : 0;

		w = policy.load(&dst_view[i - 1][j]) - u[i - 1] * w;
		policy.store(&dst_view[i - 1][j], w);
	}
}


/**
 * Base class for implementations of unresizing filter.
 */
class UnresizeImpl {
protected:
	/**
	 * Coefficients for the horizontal pass.
	 */
	BilinearContext m_hcontext;

	/**
	 * Coefficients for the vertical pass.
	 */
	BilinearContext m_vcontext;

	/**
	 * Initialize the implementation with the given coefficients.
	 *
	 * @param hcontext horizontal coefficients
	 * @param vcontext vertical coefficients
	 */
	UnresizeImpl(const BilinearContext &hcontext, const BilinearContext &vcontext);
public:
	/**
	 * Destroy implementation
	 */
	virtual ~UnresizeImpl() = 0;

	virtual void process_f16_h(const ImageTile &src, const ImageTile &dst, void *tmp) const = 0;

	virtual void process_f16_v(const ImageTile &src, const ImageTile &dst, void *tmp) const = 0;

	virtual void process_f32_h(const ImageTile &src, const ImageTile &dst, void *tmp) const = 0;

	virtual void process_f32_v(const ImageTile &src, const ImageTile &dst, void *tmp) const = 0;
};

/**
 * Create and allocate a execution kernel.
 *
 * @see Unresize::Unresize
 */
UnresizeImpl *create_unresize_impl(int src_width, int src_height, int dst_width, int dst_height, float shift_w, float shift_h, CPUClass cpu);

} // namespace unresize
} // namespace zimg

#endif // ZIMG_UNRESIZE_UNRESIZE_IMPL_H_

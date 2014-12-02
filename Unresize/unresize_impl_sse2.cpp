#ifdef ZIMG_X86

#include <cstdint>
#include <emmintrin.h>
#include "Common/except.h"
#include "Common/osdep.h"
#include "Common/tile.h"
#include "bilinear.h"
#include "unresize_impl.h"
#include "unresize_impl_x86.h"

namespace zimg {;
namespace unresize {;

namespace {;

inline FORCE_INLINE void transpose4_ps(__m128 &x0, __m128 &x1, __m128 &x2, __m128 &x3)
{
	__m128d t0 = _mm_castps_pd(_mm_unpacklo_ps(x0, x1));
	__m128d t1 = _mm_castps_pd(_mm_unpacklo_ps(x2, x3));
	__m128d t2 = _mm_castps_pd(_mm_unpackhi_ps(x0, x1));
	__m128d t3 = _mm_castps_pd(_mm_unpackhi_ps(x2, x3));

	__m128d o0 = _mm_unpacklo_pd(t0, t1);
	__m128d o1 = _mm_unpackhi_pd(t0, t1);
	__m128d o2 = _mm_unpacklo_pd(t2, t3);
	__m128d o3 = _mm_unpackhi_pd(t2, t3);

	x0 = _mm_castpd_ps(o0);
	x1 = _mm_castpd_ps(o1);
	x2 = _mm_castpd_ps(o2);
	x3 = _mm_castpd_ps(o3);
}

template <bool DoLoop>
void filter_plane_h_sse2(const BilinearContext &ctx, const ImageTile &src, const ImageTile &dst, float *tmp)
{
	TileView<const float> src_view{ src };
	TileView<float> dst_view{ dst };

	const float *matrix_data = ctx.matrix_coefficients.data();
	const int *matrix_left = ctx.matrix_row_offsets.data();
	int matrix_stride = ctx.matrix_row_stride;

	int src_width = src.width;
	int src_height = src.height;

	const float *pc = ctx.lu_c.data();
	const float *pl = ctx.lu_l.data();
	const float *pu = ctx.lu_u.data();

	for (int i = 0; i < floor_n(src_height, 4); i += 4) {
		const float *src_ptr0 = src_view[i + 0];
		const float *src_ptr1 = src_view[i + 1];
		const float *src_ptr2 = src_view[i + 2];
		const float *src_ptr3 = src_view[i + 3];

		float *dst_ptr0 = dst_view[i + 0];
		float *dst_ptr1 = dst_view[i + 1];
		float *dst_ptr2 = dst_view[i + 2];
		float *dst_ptr3 = dst_view[i + 3];

		int j;

		// Input, matrix-vector product, and forward substitution loop.
		__m128 z = _mm_setzero_ps();
		for (j = 0; j < ctx.dst_width; ++j) {
			const float *matrix_row = &matrix_data[j * matrix_stride];
			int left = matrix_left[j];

			if (left + matrix_stride > src_width)
				break;

			// Matrix-vector product.
			__m128 accum0 = _mm_setzero_ps();
			__m128 accum1 = _mm_setzero_ps();
			for (int k = 0; k < (DoLoop ? ctx.matrix_row_size : 4); k += 4) {
				__m128 coeffs = _mm_loadu_ps(&matrix_row[k]);
				__m128 v0, v1, v2, v3;

				v0 = _mm_loadu_ps(&src_ptr0[left + k]);
				v0 = _mm_mul_ps(coeffs, v0);

				v1 = _mm_loadu_ps(&src_ptr1[left + k]);
				v1 = _mm_mul_ps(coeffs, v1);

				v2 = _mm_loadu_ps(&src_ptr2[left + k]);
				v2 = _mm_mul_ps(coeffs, v2);

				v3 = _mm_loadu_ps(&src_ptr3[left + k]);
				v3 = _mm_mul_ps(coeffs, v3);

				transpose4_ps(v0, v1, v2, v3);

				accum0 = _mm_add_ps(accum0, v0);
				accum1 = _mm_add_ps(accum1, v1);
				accum0 = _mm_add_ps(accum0, v2);
				accum1 = _mm_add_ps(accum1, v3);
			}

			// Forward substitution.
			__m128 f = _mm_add_ps(accum0, accum1);
			__m128 c = _mm_set_ps1(pc[j]);
			__m128 l = _mm_set_ps1(pl[j]);

			z = _mm_mul_ps(c, z);
			z = _mm_sub_ps(f, z);
			z = _mm_mul_ps(z, l);

			_mm_store_ps(&tmp[j * 4], z);
		}
		// Handle remainder of line.
		for (; j < ctx.dst_width; ++j) {
			const float *matrix_row = &matrix_data[j * matrix_stride];
			int left = matrix_left[j];

			for (int ii = 0; ii < 4; ++ii) {
				float accum = 0;

				for (int k = 0; k < ctx.matrix_row_size; ++k) {
					accum += matrix_row[k] * src_view[i + ii][left + k];
				}
				tmp[j * 4 + ii] = (accum - pc[j] * tmp[(j - 1) * 4 + ii]) * pl[j];
			}
		}

		// Backward substitution and output loop.
		__m128 w = _mm_setzero_ps();
		for (int j = ctx.dst_width; j > floor_n(ctx.dst_width, 4); --j) {
			float w_buf[4];

			_mm_storeu_ps(w_buf, w);
			for (int ii = 0; ii < 4; ++ii) {
				w_buf[ii] = tmp[(j - 1) * 4 + ii] - pu[j - 1] * w_buf[ii];
				dst_view[i + ii][j - 1] = w_buf[ii];
			}
			w = _mm_loadu_ps(w_buf);
		}
		for (ptrdiff_t j = floor_n(ctx.dst_width, 4); j > 0; j -= 4) {
			__m128 u0, u1, u2, u3;
			__m128 z0, z1, z2, z3;
			__m128 w0, w1, w2, w3;

			z3 = _mm_load_ps(&tmp[(j - 1) * 4]);
			z2 = _mm_load_ps(&tmp[(j - 2) * 4]);
			z1 = _mm_load_ps(&tmp[(j - 3) * 4]);
			z0 = _mm_load_ps(&tmp[(j - 4) * 4]);

			u3 = _mm_set_ps1(pu[j - 1]);
			w = _mm_mul_ps(u3, w);
			w = _mm_sub_ps(z3, w);
			w3 = w;

			u2 = _mm_set_ps1(pu[j - 2]);
			w = _mm_mul_ps(u2, w);
			w = _mm_sub_ps(z2, w);
			w2 = w;

			u1 = _mm_set_ps1(pu[j - 3]);
			w = _mm_mul_ps(u1, w);
			w = _mm_sub_ps(z1, w);
			w1 = w;

			u0 = _mm_set_ps1(pu[j - 4]);
			w = _mm_mul_ps(u0, w);
			w = _mm_sub_ps(z0, w);
			w0 = w;

			transpose4_ps(w0, w1, w2, w3);

			_mm_store_ps(&dst_ptr0[j - 4], w0);
			_mm_store_ps(&dst_ptr1[j - 4], w1);
			_mm_store_ps(&dst_ptr2[j - 4], w2);
			_mm_store_ps(&dst_ptr3[j - 4], w3);
		}
	}
	for (int i = floor_n(src_height, 4); i < src_height; ++i) {
		filter_scanline_h_forward(ctx, src, tmp, i, 0, ctx.dst_width, ScalarPolicy_F32{});
		filter_scanline_h_back(ctx, tmp, dst, i, ctx.dst_width, 0, ScalarPolicy_F32{});
	}
}

void filter_plane_v_sse2(const BilinearContext &ctx, const ImageTile &src, const ImageTile &dst)
{
	TileView<const float> src_view{ src };
	TileView<float> dst_view{ dst };

	const float *matrix_data = ctx.matrix_coefficients.data();
	const int *matrix_left = ctx.matrix_row_offsets.data();
	int matrix_stride = ctx.matrix_row_stride;

	int src_width = src.width;

	const float *pc = ctx.lu_c.data();
	const float *pl = ctx.lu_l.data();
	const float *pu = ctx.lu_u.data();

	for (int i = 0; i < ctx.dst_width; ++i) {
		const float *matrix_row = &matrix_data[i * matrix_stride];
		int top = matrix_left[i];

		float *dst_ptr = dst_view[i];

		// Matrix-vector product.
		for (int k = 0; k < floor_n(ctx.matrix_row_size, 4); k += 4) {
			const float *src_ptr0 = src_view[top + k + 0];
			const float *src_ptr1 = src_view[top + k + 1];
			const float *src_ptr2 = src_view[top + k + 2];
			const float *src_ptr3 = src_view[top + k + 3];

			__m128 coeff0 = _mm_set_ps1(matrix_row[k + 0]);
			__m128 coeff1 = _mm_set_ps1(matrix_row[k + 1]);
			__m128 coeff2 = _mm_set_ps1(matrix_row[k + 2]);
			__m128 coeff3 = _mm_set_ps1(matrix_row[k + 3]);

			for (int j = 0; j < floor_n(src_width, 4); j += 4) {
				__m128 x0, x1, x2, x3;
				__m128 accum0, accum1;

				x0 = _mm_load_ps(&src_ptr0[j]);
				accum0 = _mm_mul_ps(coeff0, x0);

				x1 = _mm_load_ps(&src_ptr1[j]);
				accum1 = _mm_mul_ps(coeff1, x1);

				x2 = _mm_load_ps(&src_ptr2[j]);
				x2 = _mm_mul_ps(coeff2, x2);
				accum0 = _mm_add_ps(accum0, x2);

				x3 = _mm_load_ps(&src_ptr3[j]);
				x3 = _mm_mul_ps(coeff3, x3);
				accum1 = _mm_add_ps(accum1, x3);

				accum0 = _mm_add_ps(accum0, accum1);

				if (k)
					accum0 = _mm_add_ps(accum0, _mm_load_ps(&dst_ptr[j]));

				_mm_store_ps(&dst_ptr[j], accum0);
			}
		}
		if (ctx.matrix_row_size % 4) {
			int m = ctx.matrix_row_size % 4;
			int k = ctx.matrix_row_size - m;

			const float *src_ptr0 = src_view[top + k + 0];
			const float *src_ptr1 = src_view[top + k + 1];
			const float *src_ptr2 = src_view[top + k + 2];

			__m128 coeff0 = _mm_set_ps1(matrix_row[k + 0]);
			__m128 coeff1 = _mm_set_ps1(matrix_row[k + 1]);
			__m128 coeff2 = _mm_set_ps1(matrix_row[k + 2]);

			for (int j = 0; j < floor_n(src_width, 4); j += 4) {
				__m128 x0, x1, x2;

				__m128 accum0 = _mm_setzero_ps();
				__m128 accum1 = _mm_setzero_ps();

				switch (m) {
				case 3:
					x2 = _mm_load_ps(&src_ptr2[j]);
					accum0 = _mm_mul_ps(coeff2, x2);
				case 2:
					x1 = _mm_load_ps(&src_ptr1[j]);
					accum1 = _mm_mul_ps(coeff1, x1);
				case 1:
					x0 = _mm_load_ps(&src_ptr0[j]);
					x0 = _mm_mul_ps(coeff0, x0);
					accum0 = _mm_add_ps(accum0, x0);
				}

				accum0 = _mm_add_ps(accum0, accum1);

				if (k)
					accum0 = _mm_add_ps(accum0, _mm_load_ps(&dst_ptr[j]));

				_mm_store_ps(&dst_ptr[j], accum0);
			}
		}

		// Forward substitution.
		__m128 c = _mm_set_ps1(pc[i]);
		__m128 l = _mm_set_ps1(pl[i]);

		const float *dst_prev = i ? dst_view[i - 1] : nullptr;

		for (int j = 0; j < floor_n(src_width, 4); j += 4) {
			__m128 z = i ? _mm_load_ps(&dst_prev[j]) : _mm_setzero_ps();
			__m128 f = _mm_load_ps(&dst_ptr[j]);

			z = _mm_mul_ps(c, z);
			z = _mm_sub_ps(f, z);
			z = _mm_mul_ps(z, l);

			_mm_store_ps(&dst_ptr[j], z);
		}
		filter_scanline_v_forward(ctx, src, dst, i, floor_n(src_width, 4), src_width, ScalarPolicy_F32{});
	}

	// Back substitution.
	for (int i = ctx.dst_width; i > 0; --i) {
		__m128 u = _mm_set_ps1(pu[i - 1]);

		const float *dst_prev = i < ctx.dst_width ? dst_view[i] : nullptr;
		float *dst_ptr = dst_view[i - 1];

		for (int j = 0; j < floor_n(src_width, 4); j += 4) {
			__m128 w = i < ctx.dst_width ? _mm_load_ps(&dst_prev[j]) : _mm_setzero_ps();
			__m128 z = _mm_load_ps(&dst_ptr[j]);
			
			w = _mm_mul_ps(u, w);
			w = _mm_sub_ps(z, w);

			_mm_store_ps(&dst_ptr[j], w);
		}
		filter_scanline_v_back(ctx, dst, i, floor_n(src_width, 4), src_width, ScalarPolicy_F32{});
	}
}

class UnresizeImplH_SSE2 : public UnresizeImpl {
public:
	UnresizeImplH_SSE2(const BilinearContext context) : UnresizeImpl(context)
	{}

	void process_f16(const ImageTile &src, const ImageTile &dst, void *tmp) const override
	{
		throw ZimgUnsupportedError{ "f16 not supported in SSE2 impl" };
	}

	void process_f32(const ImageTile &src, const ImageTile &dst, void *tmp) const override
	{
		if (m_context.matrix_row_size > 4)
			filter_plane_h_sse2<true>(m_context, src, dst, (float *)tmp);
		else
			filter_plane_h_sse2<false>(m_context, src, dst, (float *)tmp);
	}
};

class UnresizeImplV_SSE2 : public UnresizeImpl {
public:
	UnresizeImplV_SSE2(const BilinearContext context) : UnresizeImpl(context)
	{}

	void process_f16(const ImageTile &src, const ImageTile &dst, void *tmp) const override
	{
		throw ZimgUnsupportedError{ "f16 not supported in SSE2 impl" };
	}

	void process_f32(const ImageTile &src, const ImageTile &dst, void *tmp) const override
	{
		filter_plane_v_sse2(m_context, src, dst);
	}
};

} // namespace


UnresizeImpl *create_unresize_impl_h_sse2(const BilinearContext &context)
{
	return new UnresizeImplH_SSE2{ context };
}

UnresizeImpl *create_unresize_impl_v_sse2(const BilinearContext &context)
{
	return new UnresizeImplV_SSE2{ context };
}

} // namespace unresize
} // namespace zimg

#endif // ZIMG_X86

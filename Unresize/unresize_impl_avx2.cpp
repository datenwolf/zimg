#ifdef ZIMG_X86

#include <cstdint>
#include <immintrin.h>
#include "Common/osdep.h"
#include "Common/tile.h"
#include "bilinear.h"
#include "unresize_impl.h"
#include "unresize_impl_x86.h"

namespace zimg {;
namespace unresize {;

namespace {;

struct VectorPolicy_F16 {
	typedef uint16_t data_type;

	FORCE_INLINE __m256 load_8(const uint16_t *src) { return _mm256_cvtph_ps(_mm_load_si128((const __m128i *)src)); }
	FORCE_INLINE __m256 loadu_8(const uint16_t *src) { return _mm256_cvtph_ps(_mm_loadu_si128((const __m128i *)src)); }

	FORCE_INLINE void store_8(uint16_t *dst, __m256 x) { _mm_store_si128((__m128i *)dst, _mm256_cvtps_ph(x, 0)); }

	FORCE_INLINE float load(const uint16_t *src) { return _mm_cvtss_f32(_mm_cvtph_ps(_mm_set1_epi16(*src))); }

	FORCE_INLINE void store(uint16_t *dst, float x) { *dst = _mm_extract_epi16(_mm_cvtps_ph(_mm_set_ps1(x), 0), 0); }
};

struct VectorPolicy_F32 : public ScalarPolicy_F32 {
	FORCE_INLINE __m256 load_8(const float *src) { return _mm256_load_ps(src); }
	FORCE_INLINE __m256 loadu_8(const float *src) { return _mm256_loadu_ps(src); }

	FORCE_INLINE void store_8(float *dst, __m256 x) { _mm256_store_ps(dst, x); }
};

inline FORCE_INLINE void transpose8_ps(__m256 &row0, __m256 &row1, __m256 &row2, __m256 &row3, __m256 &row4, __m256 &row5, __m256 &row6, __m256 &row7)
{
	__m256 t0, t1, t2, t3, t4, t5, t6, t7;
	__m256 tt0, tt1, tt2, tt3, tt4, tt5, tt6, tt7;

	t0 = _mm256_unpacklo_ps(row0, row1);
	t1 = _mm256_unpackhi_ps(row0, row1);
	t2 = _mm256_unpacklo_ps(row2, row3);
	t3 = _mm256_unpackhi_ps(row2, row3);
	t4 = _mm256_unpacklo_ps(row4, row5);
	t5 = _mm256_unpackhi_ps(row4, row5);
	t6 = _mm256_unpacklo_ps(row6, row7);
	t7 = _mm256_unpackhi_ps(row6, row7);

	tt0 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(1, 0, 1, 0));
	tt1 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 2, 3, 2));
	tt2 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(1, 0, 1, 0));
	tt3 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(3, 2, 3, 2));
	tt4 = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(1, 0, 1, 0));
	tt5 = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(3, 2, 3, 2));
	tt6 = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(1, 0, 1, 0));
	tt7 = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(3, 2, 3, 2));

	row0 = _mm256_permute2f128_ps(tt0, tt4, 0x20);
	row1 = _mm256_permute2f128_ps(tt1, tt5, 0x20);
	row2 = _mm256_permute2f128_ps(tt2, tt6, 0x20);
	row3 = _mm256_permute2f128_ps(tt3, tt7, 0x20);
	row4 = _mm256_permute2f128_ps(tt0, tt4, 0x31);
	row5 = _mm256_permute2f128_ps(tt1, tt5, 0x31);
	row6 = _mm256_permute2f128_ps(tt2, tt6, 0x31);
	row7 = _mm256_permute2f128_ps(tt3, tt7, 0x31);
}

template <bool DoLoop, class Policy>
void filter_plane_h_avx2(const BilinearContext &ctx, const ImageTile &src, const ImageTile &dst, typename Policy::data_type *tmp, Policy policy)
{
	typedef typename Policy::data_type data_type;

	TileView<const data_type> src_view{ src };
	TileView<data_type> dst_view{ dst };

	const float *matrix_data = ctx.matrix_coefficients.data();
	const int *matrix_left = ctx.matrix_row_offsets.data();
	int matrix_stride = ctx.matrix_row_stride;

	int src_width = src.width;
	int src_height = src.height;

	const float *pc = ctx.lu_c.data();
	const float *pl = ctx.lu_l.data();
	const float *pu = ctx.lu_u.data();

	for (int i = 0; i < mod(src_height, 8); i += 8) {
		const data_type *src_ptr0 = src_view[i + 0];
		const data_type *src_ptr1 = src_view[i + 1];
		const data_type *src_ptr2 = src_view[i + 2];
		const data_type *src_ptr3 = src_view[i + 3];
		const data_type *src_ptr4 = src_view[i + 4];
		const data_type *src_ptr5 = src_view[i + 5];
		const data_type *src_ptr6 = src_view[i + 6];
		const data_type *src_ptr7 = src_view[i + 7];

		data_type *dst_ptr0 = dst_view[i + 0];
		data_type *dst_ptr1 = dst_view[i + 1];
		data_type *dst_ptr2 = dst_view[i + 2];
		data_type *dst_ptr3 = dst_view[i + 3];
		data_type *dst_ptr4 = dst_view[i + 4];
		data_type *dst_ptr5 = dst_view[i + 5];
		data_type *dst_ptr6 = dst_view[i + 6];
		data_type *dst_ptr7 = dst_view[i + 7];

		int j;

		// Input, matrix-vector product, and forward substitution loop.
		__m256 z = _mm256_setzero_ps();
		for (j = 0; j < ctx.dst_width; ++j) {
			const float *matrix_row = &matrix_data[j * matrix_stride];
			int left = matrix_left[j];

			if (left + matrix_stride > src_width)
				break;

			// Matrix-vector product.
			__m256 accum0 = _mm256_setzero_ps();
			__m256 accum1 = _mm256_setzero_ps();
			__m256 accum2 = _mm256_setzero_ps();
			__m256 accum3 = _mm256_setzero_ps();

			for (int k = 0; k < (DoLoop ? ctx.matrix_row_size : 8); k += 8) {
				__m256 coeffs = _mm256_loadu_ps(&matrix_row[k]);
				__m256 v0, v1, v2, v3, v4, v5, v6, v7;

				v0 = policy.loadu_8(&src_ptr0[left + k]);
				v0 = _mm256_mul_ps(coeffs, v0);

				v1 = policy.loadu_8(&src_ptr1[left + k]);
				v1 = _mm256_mul_ps(coeffs, v1);

				v2 = policy.loadu_8(&src_ptr2[left + k]);
				v2 = _mm256_mul_ps(coeffs, v2);

				v3 = policy.loadu_8(&src_ptr3[left + k]);
				v3 = _mm256_mul_ps(coeffs, v3);

				v4 = policy.loadu_8(&src_ptr4[left + k]);
				v4 = _mm256_mul_ps(coeffs, v4);

				v5 = policy.loadu_8(&src_ptr5[left + k]);
				v5 = _mm256_mul_ps(coeffs, v5);

				v6 = policy.loadu_8(&src_ptr6[left + k]);
				v6 = _mm256_mul_ps(coeffs, v6);

				v7 = policy.loadu_8(&src_ptr7[left + k]);
				v7 = _mm256_mul_ps(coeffs, v7);

				transpose8_ps(v0, v1, v2, v3, v4, v5, v6, v7);

				accum0 = _mm256_add_ps(accum0, v0);
				accum1 = _mm256_add_ps(accum1, v1);
				accum2 = _mm256_add_ps(accum2, v2);
				accum3 = _mm256_add_ps(accum3, v3);
				accum0 = _mm256_add_ps(accum0, v4);
				accum1 = _mm256_add_ps(accum1, v5);
				accum2 = _mm256_add_ps(accum2, v6);
				accum3 = _mm256_add_ps(accum3, v7);
			}

			// Forward substitution.
			accum0 = _mm256_add_ps(accum0, accum2);
			accum1 = _mm256_add_ps(accum1, accum3);

			__m256 f = _mm256_add_ps(accum0, accum1);
			__m256 c = _mm256_broadcast_ss(&pc[j]);
			__m256 l = _mm256_broadcast_ss(&pl[j]);

			z = _mm256_fnmadd_ps(c, z, f);
			z = _mm256_mul_ps(z, l);

			policy.store_8(&tmp[j * 8], z);
		}
		// Handle remainder of line.
		for (; j < ctx.dst_width; ++j) {
			const float *matrix_row = &matrix_data[j * matrix_stride];
			int left = matrix_left[j];

			for (int ii = 0; ii < 8; ++ii) {
				float accum = 0;

				for (int k = 0; k < ctx.matrix_row_size; ++k) {
					accum += matrix_row[k] * policy.load(&src_view[i + ii][left + k]);
				}
				policy.store(&tmp[j * 8 + ii], (accum - pc[j] * policy.load(&tmp[(j - 1) * 8 + ii])) * pl[j]);
			}
		}

		// Backward substitution and output loop.
		__m256 w = _mm256_setzero_ps();
		for (int j = ctx.dst_width; j > mod(ctx.dst_width, 8); --j) {
			float w_buf[8];

			_mm256_storeu_ps(w_buf, w);
			for (int ii = 0; ii < 8; ++ii) {
				w_buf[ii] = policy.load(&tmp[(j - 1) * 8 + ii]) - pu[j - 1] * w_buf[ii];
				policy.store(&dst_view[i + ii][j - 1], w_buf[ii]);
			}
			w = _mm256_loadu_ps(w_buf);
		}
		for (int j = mod(ctx.dst_width, 8); j > 0; j -= 8) {
			__m256 u0, u1, u2, u3, u4, u5, u6, u7;
			__m256 z0, z1, z2, z3, z4, z5, z6, z7;
			__m256 w0, w1, w2, w3, w4, w5, w6, w7;

			z7 = policy.load_8(&tmp[(j - 1) * 8]);
			z6 = policy.load_8(&tmp[(j - 2) * 8]);
			z5 = policy.load_8(&tmp[(j - 3) * 8]);
			z4 = policy.load_8(&tmp[(j - 4) * 8]);
			z3 = policy.load_8(&tmp[(j - 5) * 8]);
			z2 = policy.load_8(&tmp[(j - 6) * 8]);
			z1 = policy.load_8(&tmp[(j - 7) * 8]);
			z0 = policy.load_8(&tmp[(j - 8) * 8]);

			u7 = _mm256_broadcast_ss(&pu[j - 1]);
			w = _mm256_fnmadd_ps(u7, w, z7);
			w7 = w;

			u6 = _mm256_broadcast_ss(&pu[j - 2]);
			w = _mm256_fnmadd_ps(u6, w, z6);
			w6 = w;

			u5 = _mm256_broadcast_ss(&pu[j - 3]);
			w = _mm256_fnmadd_ps(u5, w, z5);
			w5 = w;

			u4 = _mm256_broadcast_ss(&pu[j - 4]);
			w = _mm256_fnmadd_ps(u4, w, z4);
			w4 = w;

			u3 = _mm256_broadcast_ss(&pu[j - 5]);
			w = _mm256_fnmadd_ps(u3, w, z3);
			w3 = w;

			u2 = _mm256_broadcast_ss(&pu[j - 6]);
			w = _mm256_fnmadd_ps(u2, w, z2);
			w2 = w;

			u1 = _mm256_broadcast_ss(&pu[j - 7]);
			w = _mm256_fnmadd_ps(u1, w, z1);
			w1 = w;

			u0 = _mm256_broadcast_ss(&pu[j - 8]);
			w = _mm256_fnmadd_ps(u0, w, z0);
			w0 = w;

			transpose8_ps(w0, w1, w2, w3, w4, w5, w6, w7);

			policy.store_8(&dst_ptr0[j - 8], w0);
			policy.store_8(&dst_ptr1[j - 8], w1);
			policy.store_8(&dst_ptr2[j - 8], w2);
			policy.store_8(&dst_ptr3[j - 8], w3);
			policy.store_8(&dst_ptr4[j - 8], w4);
			policy.store_8(&dst_ptr5[j - 8], w5);
			policy.store_8(&dst_ptr6[j - 8], w6);
			policy.store_8(&dst_ptr7[j - 8], w7);
		}
	}
	for (int i = mod(src_height, 8); i < src_height; ++i) {
		filter_scanline_h_forward(ctx, src, tmp, i, 0, ctx.dst_width, policy);
		filter_scanline_h_back(ctx, tmp, dst, i, ctx.dst_width, 0, policy);
	}
}

template <class Policy>
void filter_plane_v_avx2(const BilinearContext &ctx, const ImageTile &src, const ImageTile &dst, Policy policy)
{
	typedef Policy::data_type data_type;

	TileView<const data_type> src_view{ src };
	TileView<data_type> dst_view{ dst };

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

		data_type *dst_ptr = dst_view[i];

		// Matrix-vector product.
		for (int k = 0; k < mod(ctx.matrix_row_size, 8); k += 8) {
			const data_type *src_ptr0 = src_view[top + k + 0];
			const data_type *src_ptr1 = src_view[top + k + 1];
			const data_type *src_ptr2 = src_view[top + k + 2];
			const data_type *src_ptr3 = src_view[top + k + 3];
			const data_type *src_ptr4 = src_view[top + k + 4];
			const data_type *src_ptr5 = src_view[top + k + 5];
			const data_type *src_ptr6 = src_view[top + k + 6];
			const data_type *src_ptr7 = src_view[top + k + 7];

			__m256 coeff0 = _mm256_broadcast_ss(&matrix_row[k + 0]);
			__m256 coeff1 = _mm256_broadcast_ss(&matrix_row[k + 1]);
			__m256 coeff2 = _mm256_broadcast_ss(&matrix_row[k + 2]);
			__m256 coeff3 = _mm256_broadcast_ss(&matrix_row[k + 3]);
			__m256 coeff4 = _mm256_broadcast_ss(&matrix_row[k + 4]);
			__m256 coeff5 = _mm256_broadcast_ss(&matrix_row[k + 5]);
			__m256 coeff6 = _mm256_broadcast_ss(&matrix_row[k + 6]);
			__m256 coeff7 = _mm256_broadcast_ss(&matrix_row[k + 7]);
				
			for (int j = 0; j < mod(src_width, 8); j += 8) {
				__m256 x0, x1, x2, x3, x4, x5, x6, x7;
				__m256 accum0, accum1, accum2, accum3;

				x0 = policy.load_8(&src_ptr0[j]);
				accum0 = _mm256_mul_ps(coeff0, x0);

				x1 = policy.load_8(&src_ptr1[j]);
				accum1 = _mm256_mul_ps(coeff1, x1);

				x2 = policy.load_8(&src_ptr2[j]);
				accum2 = _mm256_mul_ps(coeff2, x2);

				x3 = policy.load_8(&src_ptr3[j]);
				accum3 = _mm256_mul_ps(coeff3, x3);

				x4 = policy.load_8(&src_ptr4[j]);
				accum0 = _mm256_fmadd_ps(coeff4, x4, accum0);

				x5 = policy.load_8(&src_ptr5[j]);
				accum1 = _mm256_fmadd_ps(coeff5, x5, accum1);

				x6 = policy.load_8(&src_ptr6[j]);
				accum2 = _mm256_fmadd_ps(coeff6, x6, accum2);

				x7 = policy.load_8(&src_ptr7[j]);
				accum3 = _mm256_fmadd_ps(coeff7, x7, accum3);

				accum0 = _mm256_add_ps(accum0, accum2);
				accum1 = _mm256_add_ps(accum1, accum3);
				accum0 = _mm256_add_ps(accum0, accum1);

				if (k)
					accum0 = _mm256_add_ps(accum0, policy.load_8(&dst_ptr[j]));

				policy.store_8(&dst_ptr[j], accum0);
			}
		}
		if (ctx.matrix_row_size % 8) {
			int m = ctx.matrix_row_size % 8;
			int k = ctx.matrix_row_size - m;

			const data_type *src_ptr0 = src_view[top + k + 0];
			const data_type *src_ptr1 = src_view[top + k + 1];
			const data_type *src_ptr2 = src_view[top + k + 2];
			const data_type *src_ptr3 = src_view[top + k + 3];
			const data_type *src_ptr4 = src_view[top + k + 4];
			const data_type *src_ptr5 = src_view[top + k + 5];
			const data_type *src_ptr6 = src_view[top + k + 6];

			__m256 coeff0 = _mm256_broadcast_ss(&matrix_row[k + 0]);
			__m256 coeff1 = _mm256_broadcast_ss(&matrix_row[k + 1]);
			__m256 coeff2 = _mm256_broadcast_ss(&matrix_row[k + 2]);
			__m256 coeff3 = _mm256_broadcast_ss(&matrix_row[k + 3]);
			__m256 coeff4 = _mm256_broadcast_ss(&matrix_row[k + 4]);
			__m256 coeff5 = _mm256_broadcast_ss(&matrix_row[k + 5]);
			__m256 coeff6 = _mm256_broadcast_ss(&matrix_row[k + 6]);

			for (int j = 0; j < mod(src_width, 8); j += 8) {
				__m256 x0, x1, x2, x3, x4, x5, x6;

				__m256 accum0 = _mm256_setzero_ps();
				__m256 accum1 = _mm256_setzero_ps();
				__m256 accum2 = _mm256_setzero_ps();
				__m256 accum3 = _mm256_setzero_ps();

				switch (m) {
				case 7:
					x6 = policy.load_8(&src_ptr6[j]);
					accum2 = _mm256_mul_ps(coeff6, x6);
				case 6:
					x5 = policy.load_8(&src_ptr5[j]);
					accum1 = _mm256_mul_ps(coeff5, x5);
				case 5:
					x4 = policy.load_8(&src_ptr4[j]);
					accum0 = _mm256_mul_ps(coeff4, x4);
				case 4:
					x3 = policy.load_8(&src_ptr3[j]);
					accum3 = _mm256_mul_ps(coeff3, x3);
				case 3:
					x2 = policy.load_8(&src_ptr2[j]);
					accum2 = _mm256_fmadd_ps(coeff2, x2, accum2);
				case 2:
					x1 = policy.load_8(&src_ptr1[j]);
					accum1 = _mm256_fmadd_ps(coeff1, x1, accum1);
				case 1:
					x0 = policy.load_8(&src_ptr0[j]);
					accum0 = _mm256_fmadd_ps(coeff0, x0, accum0);
				}

				accum0 = _mm256_add_ps(accum0, accum2);
				accum1 = _mm256_add_ps(accum1, accum3);
				accum0 = _mm256_add_ps(accum0, accum1);

				if (k)
					accum0 = _mm256_add_ps(accum0, policy.load_8(&dst_ptr[j]));

				policy.store_8(&dst_ptr[j], accum0);
			}
		}

		// Forward substitution.
		__m256 c = _mm256_broadcast_ss(&pc[i]);
		__m256 l = _mm256_broadcast_ss(&pl[i]);

		const data_type *dst_prev = i ? dst_view[i - 1] : nullptr;

		for (int j = 0; j < mod(src_width, 8); j += 8) {
			__m256 z = i ? policy.load_8(&dst_prev[j]) : _mm256_setzero_ps();
			__m256 f = policy.load_8(&dst_ptr[j]);

			z = _mm256_fnmadd_ps(c, z, f);
			z = _mm256_mul_ps(z, l);

			policy.store_8(&dst_ptr[j], z);
		}
		
		filter_scanline_v_forward(ctx, src, dst, i, mod(src_width, 8), src_width, policy);
	}

	// Back substitution.
	for (int i = ctx.dst_width; i > 0; --i) {
		__m256 u = _mm256_broadcast_ss(pu + i - 1);

		const data_type *dst_prev = i < ctx.dst_width ? dst_view[i] : nullptr;
		data_type *dst_ptr = dst_view[i - 1];

		for (int j = 0; j < mod(src_width, 8); j += 8) {
			__m256 w = i < ctx.dst_width ? policy.load_8(&dst_prev[j]) : _mm256_setzero_ps();
			__m256 z = policy.load_8(&dst_ptr[j]);

			w = _mm256_fnmadd_ps(u, w, z);
			policy.store_8(&dst_ptr[j], w);
		}
		filter_scanline_v_back(ctx, dst, i, mod(src_width, 8), src_width, policy);
	}
}

class UnresizeImplH_AVX2 : public UnresizeImpl {
public:
	UnresizeImplH_AVX2(const BilinearContext &context) : UnresizeImpl(context)
	{}

	void process_f16(const ImageTile &src, const ImageTile &dst, void *tmp) const override
	{
		if (m_context.matrix_row_size > 8)
			filter_plane_h_avx2<true>(m_context, src, dst, (uint16_t *)tmp, VectorPolicy_F16{});
		else
			filter_plane_h_avx2<false>(m_context, src, dst, (uint16_t *)tmp, VectorPolicy_F16{});
	}

	void process_f32(const ImageTile &src, const ImageTile &dst, void *tmp) const override
	{
		if (m_context.matrix_row_size > 8)
			filter_plane_h_avx2<true>(m_context, src, dst, (float *)tmp, VectorPolicy_F32{});
		else
			filter_plane_h_avx2<false>(m_context, src, dst, (float *)tmp, VectorPolicy_F32{});
	}
};

class UnresizeImplV_AVX2 : public UnresizeImpl {
public:
	UnresizeImplV_AVX2(const BilinearContext &context) : UnresizeImpl(context)
	{}

	void process_f16(const ImageTile &src, const ImageTile &dst, void *) const override
	{
		filter_plane_v_avx2(m_context, src, dst, VectorPolicy_F16{});
	}

	void process_f32(const ImageTile &src, const ImageTile &dst, void *) const override
	{
		filter_plane_v_avx2(m_context, src, dst, VectorPolicy_F32{});
	}
};

} // namespace


UnresizeImpl *create_unresize_impl_h_avx2(const BilinearContext &context)
{
	return new UnresizeImplH_AVX2{ context };
}

UnresizeImpl *create_unresize_impl_v_avx2(const BilinearContext &context)
{
	return new UnresizeImplV_AVX2{ context };
}

} // namespace unresize
} // namespace zimg

#endif // ZIMG_X86

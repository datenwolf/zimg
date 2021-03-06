#ifdef ZIMG_X86

#include <cstddef>
#include <cstdint>
#include <emmintrin.h>
#include "Common/align.h"
#include "Common/except.h"
#include "Common/osdep.h"
#include "Common/pixel.h"
#include "Common/tile.h"
#include "resize_impl.h"
#include "resize_impl_x86.h"

namespace zimg {;
namespace resize {;

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

inline FORCE_INLINE void transpose4_epi32(__m128i &x0, __m128i &x1, __m128i &x2, __m128i &x3)
{
	__m128 tmp0 = _mm_castsi128_ps(x0);
	__m128 tmp1 = _mm_castsi128_ps(x1);
	__m128 tmp2 = _mm_castsi128_ps(x2);
	__m128 tmp3 = _mm_castsi128_ps(x3);

	transpose4_ps(tmp0, tmp1, tmp2, tmp3);

	x0 = _mm_castps_si128(tmp0);
	x1 = _mm_castps_si128(tmp1);
	x2 = _mm_castps_si128(tmp2);
	x3 = _mm_castps_si128(tmp3);
}

inline FORCE_INLINE __m128i mhadd_epi16_epi32(__m128i a, __m128i b)
{
	__m128i lo, hi, uplo, uphi;

	lo = _mm_mullo_epi16(a, b);
	hi = _mm_mulhi_epi16(a, b);

	uplo = _mm_unpacklo_epi16(lo, hi);
	uphi = _mm_unpackhi_epi16(lo, hi);

	return _mm_add_epi32(uplo, uphi);
}

inline FORCE_INLINE void fmadd_epi16_epi32(__m128i a, __m128i b, __m128i &accum0, __m128i &accum1)
{
	__m128i lo, hi, uplo, uphi;

	lo = _mm_mullo_epi16(a, b);
	hi = _mm_mulhi_epi16(a, b);

	uplo = _mm_unpacklo_epi16(lo, hi);
	uphi = _mm_unpackhi_epi16(lo, hi);

	accum0 = _mm_add_epi32(accum0, uplo);
	accum1 = _mm_add_epi32(accum1, uphi);
}

inline FORCE_INLINE __m128i pack_i30_epi32(__m128i lo, __m128i hi)
{
	__m128i offset = _mm_set1_epi32(1 << 13);

	lo = _mm_add_epi32(lo, offset);
	hi = _mm_add_epi32(hi, offset);

	lo = _mm_srai_epi32(lo, 14);
	hi = _mm_srai_epi32(hi, 14);

	return  _mm_packs_epi32(lo, hi);
}

template <bool DoLoop>
void resize_tile_u16_h_sse2(const EvaluatedFilter &filter, const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, int n)
{
	__m128i INT16_MIN_EPI16 = _mm_set1_epi16(INT16_MIN);

	int filter_stride = filter.stride_i16();

	const int16_t *filter_data = &filter.data_i16()[n * filter_stride];
	const int *filter_left = &filter.left()[n];

	int left_base = filter_left[0];

	for (int i = 0; i < TILE_HEIGHT; i += 4) {
		const uint16_t *src_p0 = src[i + 0];
		const uint16_t *src_p1 = src[i + 1];
		const uint16_t *src_p2 = src[i + 2];
		const uint16_t *src_p3 = src[i + 3];

		uint16_t *dst_p0 = dst[i + 0];
		uint16_t *dst_p1 = dst[i + 1];
		uint16_t *dst_p2 = dst[i + 2];
		uint16_t *dst_p3 = dst[i + 3];

		for (int j = 0; j < TILE_WIDTH; ++j) {
			__m128i accum = _mm_setzero_si128();
			__m128i cached[8];

			const int16_t *filter_row = &filter_data[j * filter_stride];
			int left = filter_left[j] - left_base;

			for (int k = 0; k < (DoLoop ? filter.width() : 8); k += 8) {
				__m128i coeff = _mm_load_si128((const __m128i *)&filter_row[k]);
				__m128i x0, x1, x2, x3;

				x0 = _mm_loadu_si128((const __m128i *)&src_p0[left + k]);
				x0 = _mm_add_epi16(x0, INT16_MIN_EPI16);
				x0 = mhadd_epi16_epi32(coeff, x0);

				x1 = _mm_loadu_si128((const __m128i *)&src_p1[left + k]);
				x1 = _mm_add_epi16(x1, INT16_MIN_EPI16);
				x1 = mhadd_epi16_epi32(coeff, x1);

				x2 = _mm_loadu_si128((const __m128i *)&src_p2[left + k]);
				x2 = _mm_add_epi16(x2, INT16_MIN_EPI16);
				x2 = mhadd_epi16_epi32(coeff, x2);

				x3 = _mm_loadu_si128((const __m128i *)&src_p3[left + k]);
				x3 = _mm_add_epi16(x3, INT16_MIN_EPI16);
				x3 = mhadd_epi16_epi32(coeff, x3);

				transpose4_epi32(x0, x1, x2, x3);

				x0 = _mm_add_epi32(x0, x2);
				x1 = _mm_add_epi32(x1, x3);

				accum = _mm_add_epi32(accum, x0);
				accum = _mm_add_epi32(accum, x1);
			}
			cached[j % 8] = accum;

			if (j % 8 == 7) {
				int dst_j = floor_n(j, 8);
				__m128i packed;

				transpose4_epi32(cached[0], cached[1], cached[2], cached[3]);
				transpose4_epi32(cached[4], cached[5], cached[6], cached[7]);

				packed = pack_i30_epi32(cached[0], cached[4]);
				packed = _mm_sub_epi16(packed, INT16_MIN_EPI16);
				_mm_store_si128((__m128i *)&dst_p0[dst_j], packed);

				packed = pack_i30_epi32(cached[1], cached[5]);
				packed = _mm_sub_epi16(packed, INT16_MIN_EPI16);
				_mm_store_si128((__m128i *)&dst_p1[dst_j], packed);

				packed = pack_i30_epi32(cached[2], cached[6]);
				packed = _mm_sub_epi16(packed, INT16_MIN_EPI16);
				_mm_store_si128((__m128i *)&dst_p2[dst_j], packed);

				packed = pack_i30_epi32(cached[3], cached[7]);
				packed = _mm_sub_epi16(packed, INT16_MIN_EPI16);
				_mm_store_si128((__m128i *)&dst_p3[dst_j], packed);
			}
		}
	}
}

template <bool DoLoop>
void resize_tile_fp_h_sse2(const EvaluatedFilter &filter, const ImageTile<const float> &src, const ImageTile<float> &dst, int n)
{
	int filter_stride = filter.stride();

	const float *filter_data = &filter.data()[n * filter_stride];
	const int *filter_left = &filter.left()[n];

	int left_base = filter_left[0];

	for (int i = 0; i < TILE_HEIGHT; i += 4) {
		const float *src_p0 = src[i + 0];
		const float *src_p1 = src[i + 1];
		const float *src_p2 = src[i + 2];
		const float *src_p3 = src[i + 3];

		float *dst_p0 = dst[i + 0];
		float *dst_p1 = dst[i + 1];
		float *dst_p2 = dst[i + 2];
		float *dst_p3 = dst[i + 3];

		for (int j = 0; j < TILE_WIDTH; ++j) {
			__m128 accum = _mm_setzero_ps();
			__m128 cached[4];

			const float *filter_row = &filter_data[j * filter_stride];
			int left = filter_left[j] - left_base;

			for (int k = 0; k < (DoLoop ? filter.width() : 4); k += 4) {
				__m128 coeff = _mm_load_ps(filter_row + k);
				__m128 x0, x1, x2, x3;

				x0 = _mm_loadu_ps(&src_p0[left + k]);
				x0 = _mm_mul_ps(coeff, x0);

				x1 = _mm_loadu_ps(&src_p1[left + k]);
				x1 = _mm_mul_ps(coeff, x1);

				x2 = _mm_loadu_ps(&src_p2[left + k]);
				x2 = _mm_mul_ps(coeff, x2);

				x3 = _mm_loadu_ps(&src_p3[left + k]);
				x3 = _mm_mul_ps(coeff, x3);

				transpose4_ps(x0, x1, x2, x3);

				x0 = _mm_add_ps(x0, x2);
				x1 = _mm_add_ps(x1, x3);
				
				accum = _mm_add_ps(accum, x0);
				accum = _mm_add_ps(accum, x1);
			}
			cached[j % 4] = accum;

			if (j % 4 == 3) {
				int dst_j = floor_n(j, 4);

				transpose4_ps(cached[0], cached[1], cached[2], cached[3]);

				_mm_store_ps(&dst_p0[dst_j], cached[0]);
				_mm_store_ps(&dst_p1[dst_j], cached[1]);
				_mm_store_ps(&dst_p2[dst_j], cached[2]);
				_mm_store_ps(&dst_p3[dst_j], cached[3]);
			}
		}
	}
}

void resize_tile_u16_v_sse2(const EvaluatedFilter &filter, const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, int n)
{
	__m128i INT16_MIN_EPI16 = _mm_set1_epi16(INT16_MIN);
	__m128i tmp_m128i[TILE_WIDTH / 4];
	uint32_t *tmp = (uint32_t *)tmp_m128i;

	int filter_stride = filter.stride_i16();

	const int16_t *filter_data = &filter.data_i16()[filter_stride * n];
	const int *filter_left = &filter.left()[n];

	int top_base = filter_left[0];

	for (int i = 0; i < TILE_HEIGHT; ++i) {
		const int16_t *filter_row = &filter_data[i * filter_stride];
		int top = filter_left[i] - top_base;
		uint16_t *dst_ptr = dst[i];

		for (int k = 0; k < floor_n(filter.width(), 4); k += 4) {
			const uint16_t *src_ptr0 = src[top + k + 0];
			const uint16_t *src_ptr1 = src[top + k + 1];
			const uint16_t *src_ptr2 = src[top + k + 2];
			const uint16_t *src_ptr3 = src[top + k + 3];

			__m128i coeff0 = _mm_set1_epi16(filter_row[k + 0]);
			__m128i coeff1 = _mm_set1_epi16(filter_row[k + 1]);
			__m128i coeff2 = _mm_set1_epi16(filter_row[k + 2]);
			__m128i coeff3 = _mm_set1_epi16(filter_row[k + 3]);

			for (int j = 0; j < TILE_WIDTH; j += 8) {
				__m128i x0, x1, x2, x3;
				__m128i packed;

				__m128i accum0l = _mm_setzero_si128();
				__m128i accum0h = _mm_setzero_si128();
				__m128i accum1l = _mm_setzero_si128();
				__m128i accum1h = _mm_setzero_si128();

				x0 = _mm_load_si128((const __m128i *)&src_ptr0[j]);
				x0 = _mm_add_epi16(x0, INT16_MIN_EPI16);
				fmadd_epi16_epi32(coeff0, x0, accum0l, accum0h);

				x1 = _mm_load_si128((const __m128i *)&src_ptr1[j]);
				x1 = _mm_add_epi16(x1, INT16_MIN_EPI16);
				fmadd_epi16_epi32(coeff1, x1, accum1l, accum1h);

				x2 = _mm_load_si128((const __m128i *)&src_ptr2[j]);
				x2 = _mm_add_epi16(x2, INT16_MIN_EPI16);
				fmadd_epi16_epi32(coeff2, x2, accum0l, accum0h);

				x3 = _mm_load_si128((const __m128i *)&src_ptr3[j]);
				x3 = _mm_add_epi16(x3, INT16_MIN_EPI16);
				fmadd_epi16_epi32(coeff3, x3, accum1l, accum1h);

				accum0l = _mm_add_epi32(accum0l, accum1l);
				accum0h = _mm_add_epi32(accum0h, accum1h);

				if (k) {
					accum0l = _mm_add_epi32(accum0l, _mm_load_si128((const __m128i *)&tmp[j * 2 + 0]));
					accum0h = _mm_add_epi32(accum0h, _mm_load_si128((const __m128i *)&tmp[j * 2 + 4]));
				}

				if (k == filter.width() - 4) {
					packed = pack_i30_epi32(accum0l, accum0h);
					packed = _mm_sub_epi16(packed, INT16_MIN_EPI16);
					_mm_store_si128((__m128i *)&dst_ptr[j], packed);
				} else {
					_mm_store_si128((__m128i *)&tmp[j * 2 + 0], accum0l);
					_mm_store_si128((__m128i *)&tmp[j * 2 + 4], accum0h);
				}
			}
		}
		if (filter.width() % 4) {
			int m = filter.width() % 4;
			int k = filter.width() - m;

			const uint16_t *src_ptr0 = src[top + k + 0];
			const uint16_t *src_ptr1 = src[top + k + 1];
			const uint16_t *src_ptr2 = src[top + k + 2];

			__m128i coeff0 = _mm_set1_epi16(filter_row[k + 0]);
			__m128i coeff1 = _mm_set1_epi16(filter_row[k + 1]);
			__m128i coeff2 = _mm_set1_epi16(filter_row[k + 2]);

			for (int j = 0; j < TILE_WIDTH; j += 8) {
				__m128i x0, x1, x2;
				__m128i packed;

				__m128i accum0l = _mm_setzero_si128();
				__m128i accum0h = _mm_setzero_si128();
				__m128i accum1l = _mm_setzero_si128();
				__m128i accum1h = _mm_setzero_si128();

				switch (m) {
				case 3:
					x2 = _mm_load_si128((const __m128i *)&src_ptr2[j]);
					x2 = _mm_add_epi16(x2, INT16_MIN_EPI16);
					fmadd_epi16_epi32(coeff2, x2, accum0l, accum0h);
				case 2:
					x1 = _mm_load_si128((const __m128i *)&src_ptr1[j]);
					x1 = _mm_add_epi16(x1, INT16_MIN_EPI16);
					fmadd_epi16_epi32(coeff1, x1, accum1l, accum1h);
				case 1:
					x0 = _mm_load_si128((const __m128i *)&src_ptr0[j]);
					x0 = _mm_add_epi16(x0, INT16_MIN_EPI16);
					fmadd_epi16_epi32(coeff0, x0, accum0l, accum0h);
				}

				accum0l = _mm_add_epi32(accum0l, accum1l);
				accum0h = _mm_add_epi32(accum0h, accum1h);

				if (k) {
					accum0l = _mm_add_epi32(accum0l, _mm_load_si128((const __m128i *)&tmp[j * 2 + 0]));
					accum0h = _mm_add_epi32(accum0h, _mm_load_si128((const __m128i *)&tmp[j * 2 + 4]));
				}

				packed = pack_i30_epi32(accum0l, accum0h);
				packed = _mm_sub_epi16(packed, INT16_MIN_EPI16);

				_mm_store_si128((__m128i *)&dst_ptr[j], packed);
			}
		}
	}
}

void resize_tile_fp_v_sse2(const EvaluatedFilter &filter, const ImageTile<const float> &src, const ImageTile<float> &dst, int n)
{
	int filter_stride = filter.stride();

	const float *filter_data = &filter.data()[n * filter_stride];
	const int *filter_left = &filter.left()[n];

	int top_base = filter_left[0];

	for (int i = 0; i < TILE_HEIGHT; ++i) {
		const float *filter_row = &filter_data[i * filter_stride];
		int top = filter_left[i] - top_base;
		float *dst_ptr = dst[i];

		for (int k = 0; k < floor_n(filter.width(), 4); k += 4) {
			const float *src_ptr0 = src[top + k + 0];
			const float *src_ptr1 = src[top + k + 1];
			const float *src_ptr2 = src[top + k + 2];
			const float *src_ptr3 = src[top + k + 3];

			__m128 coeff0 = _mm_set_ps1(filter_row[k + 0]);
			__m128 coeff1 = _mm_set_ps1(filter_row[k + 1]);
			__m128 coeff2 = _mm_set_ps1(filter_row[k + 2]);
			__m128 coeff3 = _mm_set_ps1(filter_row[k + 3]);

			for (int j = 0; j < TILE_WIDTH; j += 4) {
				__m128 x0, x1, x2, x3;
				__m128 accum0, accum1;

				x0 = _mm_load_ps(src_ptr0 + j);
				accum0 = _mm_mul_ps(coeff0, x0);

				x1 = _mm_load_ps(src_ptr1 + j);
				accum1 = _mm_mul_ps(coeff1, x1);

				x2 = _mm_load_ps(src_ptr2 + j);
				x2 = _mm_mul_ps(coeff2, x2);
				accum0 = _mm_add_ps(accum0, x2);

				x3 = _mm_load_ps(src_ptr3 + j);
				x3 = _mm_mul_ps(coeff3, x3);
				accum1 = _mm_add_ps(accum1, x3);

				accum0 = _mm_add_ps(accum0, accum1);

				if (k)
					accum0 = _mm_add_ps(accum0, _mm_load_ps(&dst_ptr[j]));

				_mm_store_ps(&dst_ptr[j], accum0);
			}
		}
		if (filter.width() % 4) {
			int m = filter.width() % 4;
			int k = filter.width() - m;

			const float *src_ptr0 = src[top + k + 0];
			const float *src_ptr1 = src[top + k + 1];
			const float *src_ptr2 = src[top + k + 2];

			__m128 coeff0 = _mm_set_ps1(filter_row[k + 0]);
			__m128 coeff1 = _mm_set_ps1(filter_row[k + 1]);
			__m128 coeff2 = _mm_set_ps1(filter_row[k + 2]);

			for (int j = 0; j < TILE_WIDTH; j += 4) {
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
	}
}

class ResizeImplH_SSE2 : public ResizeImpl {
public:
	ResizeImplH_SSE2(const EvaluatedFilter &filter) : ResizeImpl(filter, true)
	{
	}

	bool pixel_supported(PixelType type) const override
	{
		return type == PixelType::WORD || type == PixelType::FLOAT;
	}

	void process_u16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, int i, int j) const override
	{
		if (m_filter.width() > 8)
			resize_tile_u16_h_sse2<true>(m_filter, src, dst, j);
		else
			resize_tile_u16_h_sse2<false>(m_filter, src, dst, j);
	}

	void process_f16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, int i, int j) const override
	{
		throw ZimgUnsupportedError{ "f16 not supported in SSE2 impl" };
	}

	void process_f32(const ImageTile<const float> &src, const ImageTile<float> &dst, int i, int j) const override
	{
		if (m_filter.width() > 4)
			resize_tile_fp_h_sse2<true>(m_filter, src, dst, j);
		else
			resize_tile_fp_h_sse2<false>(m_filter, src, dst, j);
	}
};

class ResizeImplV_SSE2 : public ResizeImpl {
public:
	ResizeImplV_SSE2(const EvaluatedFilter &filter) : ResizeImpl(filter, false)
	{
	}

	bool pixel_supported(PixelType type) const override
	{
		return type == PixelType::WORD || type == PixelType::FLOAT;
	}

	void process_u16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, int i, int j) const override
	{
		resize_tile_u16_v_sse2(m_filter, src, dst, i);
	}

	void process_f16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, int i, int j) const override
	{
		throw ZimgUnsupportedError{ "f16 not supported in SSE2 impl" };
	}

	void process_f32(const ImageTile<const float> &src, const ImageTile<float> &dst, int i, int j) const override
	{
		resize_tile_fp_v_sse2(m_filter, src, dst, i);
	}
};

} // namespace


ResizeImpl *create_resize_impl_h_sse2(const EvaluatedFilter &filter)
{
	return new ResizeImplH_SSE2{ filter };
}

ResizeImpl *create_resize_impl_v_sse2(const EvaluatedFilter &filter)
{
	return new ResizeImplV_SSE2{ filter };
}

} // namespace resize
} // namespace zimg

#endif // ZIMG_X86

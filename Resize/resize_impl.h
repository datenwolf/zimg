#pragma once

#ifndef ZIMG_RESIZE_RESIZE_IMPL_H_
#define ZIMG_RESIZE_RESIZE_IMPL_H_

#include <algorithm>
#include <cstdint>
#include "Common/osdep.h"
#include "filter.h"

namespace zimg {;

enum class CPUClass;

template <class T>
class ImageTile;

namespace resize {;

struct ScalarPolicy_U16 {
	typedef uint16_t data_type;
	typedef int32_t num_type;

	FORCE_INLINE int32_t coeff(const EvaluatedFilter &filter, int row, int k)
	{
		return filter.data_i16()[row * filter.stride_i16() + k];
	}

	FORCE_INLINE int32_t load(const uint16_t *src)
	{
		uint16_t x = *src;
		return (int32_t)x + (int32_t)INT16_MIN; // Make signed.
	}

	FORCE_INLINE void store(uint16_t *dst, int32_t x)
	{
		// Convert from 16.14 to 16.0.
		x = ((x + (1 << 13)) >> 14) - (int32_t)INT16_MIN;

		// Clamp out of range values.
		x = std::max(std::min(x, (int32_t)UINT16_MAX), (int32_t)0);

		*dst = (uint16_t)x;
	}
};

struct ScalarPolicy_F32 {
	typedef float data_type;
	typedef float num_type;

	FORCE_INLINE float coeff(const EvaluatedFilter &filter, int row, int k)
	{
		return filter.data()[row * filter.stride() + k];
	}

	FORCE_INLINE float load(const float *src) { return *src; }

	FORCE_INLINE void store(float *dst, float x) { *dst = x; }
};

template <class T, class Policy>
inline FORCE_INLINE void resize_tile_h_scalar(const EvaluatedFilter &filter, const ImageTile<const T> &src, const ImageTile<T> &dst, int n,
                                              int i_begin, int j_begin, int i_end, int j_end, Policy policy)
{
	typedef typename Policy::data_type data_type;
	typedef typename Policy::num_type num_type;

	int left_base = filter.left()[n];
	
	for (int i = i_begin; i < i_end; ++i) {
		for (int j = j_begin; j < j_end; ++j) {
			int filter_row = n + j;
			int left = filter.left()[filter_row] - left_base;

			num_type accum = 0;

			for (int k = 0; k < filter.width(); ++k) {
				num_type coeff = policy.coeff(filter, filter_row, k);
				num_type x = policy.load(&src[i][left + k]);

				accum += coeff * x;
			}

			policy.store(&dst[i][j], accum);
		}
	}
}

template <class T, class Policy>
inline FORCE_INLINE void resize_tile_v_scalar(const EvaluatedFilter &filter, const ImageTile<const T> &src, const ImageTile<T> &dst, int n,
                                              int i_begin, int j_begin, int i_end, int j_end, Policy policy)
{
	typedef typename Policy::data_type data_type;
	typedef typename Policy::num_type num_type;

	int top_base = filter.left()[n];

	for (int i = i_begin; i < i_end; ++i) {
		int filter_row = n + i;
		int top = filter.left()[filter_row] - top_base;

		for (int j = j_begin; j < j_end; ++j) {
			num_type accum = 0;

			for (int k = 0; k < filter.width(); ++k) {
				num_type coeff = policy.coeff(filter, filter_row, k);
				num_type x = policy.load(&src[top + k][j]);

				accum += coeff * x;
			}

			policy.store(&dst[i][j], accum);
		}
	}
}

/**
 * Base class for implementations of resizing filter.
 */
class ResizeImpl {
	bool m_horizontal;
protected:
	/**
	 * Filter coefficients.
	 */
	EvaluatedFilter m_filter;

	/**
	 * Initialize the implementation with the given coefficients.
	 *
	 * @param filter coefficients
	 * @param horizontal whether filter is a horizontal resize
	 */
	ResizeImpl(const EvaluatedFilter &filter, bool horizontal);
public:
	/**
	 * Destroy implementation.
	 */
	virtual ~ResizeImpl() = 0;

	/**
	 * Get the input rectangle required to process an output tile.
	 *
	 * @param dst_top output top row index
	 * @param dst_left output left column index
	 * @param dst_bottom output bottom row index
	 * @param dst_right output right column index
	 * @param src_top pointer to receive input top row index
	 * @param src_left pointer to receive input left column index
	 * @param src_bottom pointer to receive output bottom row index
	 * @param src_right pointer to receive output right column index
	 */
	void dependent_rect(int dst_top, int dst_left, int dst_bottom, int dst_right, int *src_top, int *src_left, int *src_bottom, int *src_right) const;

	/**
	 * Execute filter pass on an unsigned 16-bit image.
	 *
	 * @param src input tile
	 * @param dst output tile
	 * @param i row index of output tile
	 * @param j column index of output tile
	 * @param tmp temporary buffer (implementation defined size)
	 * @throws ZimgUnsupportedError if not supported
	 */
	virtual void process_u16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, int i, int j, void *tmp) const = 0;

	/**
	 * Execute filter pass on a half precision 16-bit image.
	 *
	 * @see ResizeImpl::process_u16_h
	 */
	virtual void process_f16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, int i, int j, void *tmp) const = 0;

	/**
	 * Execute filter pass on a single precision 32-bit image.
	 *
	 * @see ResizeImpl::process_u16_h
	 */
	virtual void process_f32(const ImageTile<const float> &src, const ImageTile<float> &dst, int i, int j, void *tmp) const = 0;
};

/**
 * Create a concrete ResizeImpl.

 * @see Resize::Resize
 */
ResizeImpl *create_resize_impl(const Filter &f, bool horizontal, int src_dim, int dst_dim, double shift, double width, CPUClass cpu);

} // namespace resize
} // namespace zimg

#endif // ZIMG_RESIZE_RESIZE_IMPL_H_

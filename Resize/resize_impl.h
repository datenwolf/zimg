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

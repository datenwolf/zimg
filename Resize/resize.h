#pragma once

#ifndef ZIMG_RESIZE_RESIZE_H_
#define ZIMG_RESIZE_RESIZE_H_

#include <cstddef>
#include <memory>

namespace zimg {;

enum class CPUClass;
enum class PixelType;

template <class T>
class ImageTile;

namespace resize {;

class Filter;
class ResizeImpl;

/**
 * Resize: applies a resizing filter.
 *
 * Each instance is applicable only for its given set of resizing parameters.
 */
class Resize {
	std::shared_ptr<ResizeImpl> m_impl;
	bool m_horizontal;
public:
	/**
	 * Initialize a null context. Cannot be used for execution.
	 */
	Resize() = default;

	/**
	 * Initialize a context to apply a given resizing filter.
	 *
	 * @param f filter
	 * @param horizontal whether resizing is to be done horizontally or vertically
	 * @param src_dim input dimension
	 * @param dst_dim output dimension
	 * @param shift center shift in units of source pixels
	 * @param width active subwindow in units of source pixels
	 * @param cpu create kernel optimized for given cpu
	 * @throws ZimgIllegalArgument on unsupported parameter combinations
	 * @throws ZimgOutOfMemory if out of memory
	 */
	Resize(const Filter &f, bool horizontal, int src_dim, int dst_dim, double shift, double width, CPUClass cpu);

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
	 * Process an image. The input and output pixel formats must match.
	 * The input tile must correspond to the required input sub-rectangle (@see Resize::dependent_rect).
	 *
	 * @param src input tile
	 * @param dst output tile
	 * @param i row index of output tile
	 * @param j column index of output tile
	 * @throws ZimgUnsupportedError if pixel type not supported
	 */
	void process(const ImageTile<const void> &src, const ImageTile<void> &dst, int i, int j) const;
};

/**
 * Check if resizing horizontally or vertically first is more efficient.
 *
 * @param xscale horizontal resizing ratio
 * @param yscale vertical resizing ratio
 * @return true if resizing horizontally first is more efficient
 */
bool resize_horizontal_first(double xscale, double yscale);

} // namespace resize
} // namespace zimg

#endif // ZIMG_RESIZE_RESIZE_H_

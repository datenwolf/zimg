#pragma once

#ifndef ZIMG_RESIZE_RESIZE_H_
#define ZIMG_RESIZE_RESIZE_H_

#include <cstddef>
#include <memory>

namespace zimg {;

enum class CPUClass;
enum class PixelType;

struct ImageTile;

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

	void invoke_impl(const ImageTile &src, const ImageTile &dst, void *tmp) const;
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
	 * Get the size of the temporary buffer required by the filter.
	 *
	 * @param type pixel type to process
	 * @param width input width
	 * @return the size of temporary buffer in units of pixels
	 */
	size_t tmp_size(PixelType type, int width) const;

	/**
	 * Process an image. The input and output pixel formats must match.
	 * The tile must span an entire plane.
	 *
	 * @param src input tile
	 * @param dst output tile
	 * @param tmp temporary buffer (@see Resize::tmp_size)
	 * @throws ZimgUnsupportedError if pixel type not supported
	 */
	void process(const ImageTile &src, const ImageTile &dst, void *tmp) const;
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

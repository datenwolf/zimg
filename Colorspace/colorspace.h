#pragma once

#ifndef ZIMG_COLORSPACE_COLORSPACE_H_
#define ZIMG_COLORSPACE_COLORSPACE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include "operation.h"

namespace zimg {;

enum class PixelType;
enum class CPUClass;

template <class T>
class ImageTile;

namespace colorspace {;

struct ColorspaceDefinition;

/**
 * ColorspaceConversion: converts between colorspaces.
 *
 * Each instance is applicable only for its given set of source and destination colorspace.
 */
class ColorspaceConversion {
	std::shared_ptr<PixelAdapter> m_pixel_adapter;
	std::vector<std::shared_ptr<Operation>> m_operations;

	void load_tile(const ImageTile<const void> &src, float *dst) const;

	void store_tile(const float *src, const ImageTile<void> &dst) const;
public:
	/**
	 * Initialize a null context. Cannot be used for execution.
	 */
	ColorspaceConversion() = default;

	/**
	 * Initialize a context to apply a given colorspace conversion.
	 *
	 * @param in input colorspace
	 * @param out output colorspace
	 * @param cpu create context optimized for given cpu
	 * @throws ZimgIllegalArgument on invalid colorspace definition
	 * @throws ZimgOutOfMemory if out of memory
	 */
	ColorspaceConversion(const ColorspaceDefinition &in, const ColorspaceDefinition &out, CPUClass cpu);

	/**
	 * Check if conversion supports the given pixel type.
	 *
	 * @param type pixel type
	 * @return true if supported, else false
	 */
	bool pixel_supported(PixelType type) const;

	/**
	 * Get the size of the temporary buffer required by the conversion.
	 *
	 * @param width width of image tile
	 * @param height height of image tile
	 * @return the size of the temporary buffer in units of floats
	 */
	size_t tmp_size(int width, int height) const;

	/**
	 * Process a tile. The input and output pixel formats must match.
	 *
	 * @param src pointer to three input tiles
	 * @param dst pointer to three output tiles
	 * @param tmp temporary buffer (@see ColorspaceConversion::tmp_size)
	 */
	void process_tile(const ImageTile<const void> src[3], const ImageTile<void> dst[3], void *tmp) const;
};

} // namespace colorspace
} // namespace zimg

#endif // ZIMG_COLORSPACE_COLORSPACE_H_

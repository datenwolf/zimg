#pragma once

#ifndef ZIMG_TILE_H_
#define ZIMG_TILE_H_

#include <algorithm>
#include <cstddef>
#include "pixel.h"

namespace zimg {;

/**
 * Descriptor for a buffer containing image data.
 */
struct ImageTile {
	/**
	 * Pointer to top-left pixel.
	 */
	void *ptr;

	/**
	 * Distance between scanlines in bytes.
	 */
	int byte_stride;

	/**
	 * Width of tile.
	 */
	int width;

	/**
	 * Height of tile.
	 */
	int height;

	/**
	 * Descriptor for image pixels.
	 */
	PixelFormat format;
};

/**
 * Helper class for indexing into image data.
 *
 * @param T data type of pixel
 */
template <class T>
class TileView {
	T *m_ptr;
	int m_byte_stride;

	T *index(int i, int j)
	{
		return const_cast<T *>(const_cast<const TileView *>(this)->index(i, j));
	}

	const T *index(int i, int j) const
	{
		const char *byte_ptr = reinterpret_cast<const char *>(m_ptr);
		ptrdiff_t row_offset = (ptrdiff_t)i * m_byte_stride;
		ptrdiff_t col_offset = (ptrdiff_t)j * sizeof(T);

		return reinterpret_cast<const T *>(byte_ptr + row_offset + col_offset);
	}

	TileView(T *ptr, int byte_stride) : m_ptr{ ptr }, m_byte_stride{ byte_stride }
	{
	}
public:
	/**
	 * Construct a view from a tile.
	 *
	 * @param tile tile
	 */
	explicit TileView(const ImageTile &tile) : TileView{ (T *)tile.ptr, tile.byte_stride }
	{
	}

	/**
	 * Get pointer to image row.
	 *
	 * @param i row
	 * @return pointer to row
	 */
	T *operator[](int i)
	{
		return index(i, 0);
	}

	/**
	 * Get read-only pointer to image row.
	 *
	 * @see TileView::operator[](unsigned)
	 */
	const T *operator[](int i) const
	{
		return index(i, 0);
	}

	/**
	 * Get view centered on a subregion of the tile.
	 *
	 * @param i row
	 * @param j column
	 * @return view centered on offset
	 */
	TileView<T> sub_view(int i, int j)
	{
		return{ index(i, j), m_byte_stride };
	}

	/**
	 * Get read-only view centered on a subregion of the tile.
	 *
	 * @see TileView::sub_view(unsigned, unsigned)
	 */
	TileView<const T> sub_view(int i, int j) const
	{
		return{ index(i, j), m_byte_stride };
	}
};

/**
 * Copy an image tile. The tiles must have identical dimensions and formats.
 *
 * @param src input tile
 * @param dst output tile
 */
inline void copy_image_tile(const ImageTile &src, const ImageTile &dst)
{
	int h = dst.height;
	int w = dst.width * pixel_size(dst.format.type);

	for (int i = 0; i < h; ++i) {
		const char *src_ptr = (const char *)src.ptr + i * src.byte_stride;
		char *dst_ptr = (char *)dst.ptr + i * dst.byte_stride;
		
		std::copy_n(src_ptr, w, dst_ptr);
	}
}

} // namespace zimg

#endif // ZIMG_TILE_H_

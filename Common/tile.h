#pragma once

#ifndef ZIMG_TILE_H_
#define ZIMG_TILE_H_

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include "pixel.h"

namespace zimg {;

const int TILE_WIDTH = 64;
const int TILE_HEIGHT = 64;

/**
 * Descriptor for plane-level metadata.
 */
struct PlaneDescriptor {
	PixelFormat format;
	int bytes_per_pixel;
	int width;
	int height;

	PlaneDescriptor() = default;

	PlaneDescriptor(PixelType type, int width = 0, int height = 0) :
		PlaneDescriptor{ default_pixel_format(type), width, height }
	{
	}

	PlaneDescriptor(PixelFormat format, int width = 0, int height = 0) :
		format(format),
		bytes_per_pixel{ pixel_size(format.type) },
		width{ width },
		height{ height }
	{
	}
};

/**
 * Wrapper around an image tile.
 *
 * @param T type of pixel data
 */
template <class T>
class ImageTile {
	typedef typename std::add_const<T>::type const_type;
	typedef typename std::remove_const<T>::type non_const_type;

	T *m_ptr;
	const PlaneDescriptor *m_descriptor;
	int m_byte_stride;

	template <class U = T>
	int x_bytes_per_pixel(typename std::enable_if<std::is_void<U>::value>::type *x = nullptr) const
	{
		return m_descriptor->bytes_per_pixel;
	}

	template <class U = T>
	int x_bytes_per_pixel(typename std::enable_if<!std::is_void<U>::value>::type *x = nullptr) const
	{
		return (int)sizeof(U);
	}

	T *address_of(int i, int j) const
	{
		const char *byte_ptr = reinterpret_cast<const char *>(m_ptr);

		byte_ptr += static_cast<ptrdiff_t>(i) * m_byte_stride;
		byte_ptr += static_cast<ptrdiff_t>(j) * bytes_per_pixel();

		return const_cast<T *>(reinterpret_cast<const_type *>(byte_ptr));
	}
public:
	/**
	 * Default construct tile. Values are uninitialized.
	 */
	ImageTile() = default;

	/**
	 * Initialize a image tile.
	 *
	 * @param ptr pointer to top-left pixel in tile
	 * @param descriptor pointer to descriptor for plane containing tile, mandatory for non-void T
	 * @param byte_stride distaince between image scanlines in bytes
	 * @param width width of tile
	 * @param height height of tile
	 */
	ImageTile(T *ptr, const PlaneDescriptor *descriptor, int byte_stride) :
		m_ptr{ ptr },
		m_descriptor{ descriptor },
		m_byte_stride{ byte_stride }
	{
	}

	/**
	 * Initialize a const tile from a non-const tile.
	 *
	 * @param other non-const tile
	 */
	template <class U>
	ImageTile(const ImageTile<U> &other,
	          typename std::enable_if<std::is_same<T, const_type>::value &&
	                                  !std::is_same<U, const_type>::value>::type *x = nullptr) :
		m_ptr{ const_cast<T *>(other.data()) },
		m_descriptor{ other.descriptor() },
		m_byte_stride{ other.byte_stride() }
	{
	}

	/**
	 * @return pointer to top-left pixel
	 */
	T *data() const
	{
		return m_ptr;
	}

	/**
	 * @return pointer to descriptor
	 */
	const PlaneDescriptor *descriptor() const
	{
		return m_descriptor;
	}

	/**
	 * @return stride in bytes
	 */
	int byte_stride() const
	{
		return m_byte_stride;
	}

	/**
	 * Get the size in bytes of a pixel.
	 *
	 * @return size of pixel
	 */
	int bytes_per_pixel() const
	{
		return x_bytes_per_pixel();
	}

	/**
	 * Get the distance between image scanlines in pixels.
	 *
	 * @return stride in pixels
	 */
	int pixel_stride() const
	{
		return m_byte_stride / bytes_per_pixel();
	}

	/**
	 * Get a pointer to a scanline.
	 *
	 * @param i row index
	 * @return pointer to left pixel of scanline
	 */
	T *operator[](int i) const
	{
		return address_of(i, 0);
	}

	/**
	 * Get a tile pointing to an offset within the tile.
	 *
	 * @param i row offset
	 * @param j column offset
	 * @return tile pointing to offset
	 */
	ImageTile sub_tile(int i, int j) const
	{
		return{ address_of(i, j), m_descriptor, m_byte_stride };
	}
};

template <class T, class U>
ImageTile<T> tile_cast(const ImageTile<U> &tile)
{
	return{ static_cast<T *>(tile.data()), tile.descriptor(), tile.byte_stride() };
}

/**
 * Copy a partial image tile. The tiles must have identical formats.
 *
 * @param src input tile
 * @param dst output tile
 * @param width columns to copy
 * @param height rows to copy
 */
template <class T>
inline void copy_image_tile_partial(const ImageTile<const T> &src, const ImageTile<T> &dst, int width, int height)
{
	for (int i = 0; i < height; ++i) {
		const char *src_ptr = reinterpret_cast<const char *>(src[i]);
		char *dst_ptr = reinterpret_cast<char *>(dst[i]);
		int line_size = width * src.bytes_per_pixel();

		std::copy_n(src_ptr, line_size, dst_ptr);
	}
}

/**
 * Copy an image tile. The tiles must have identical formats.
 *
 * @param src input tile
 * @param dst output tile
 */
template <class T>
inline void copy_image_tile(const ImageTile<const T> &src, const ImageTile<T> &dst)
{
	copy_image_tile_partial(src, dst, TILE_WIDTH, TILE_HEIGHT);
}

} // namespace zimg

#endif // ZIMG_TILE_H_

#pragma once

#ifndef ZIMG_DEPTH_DEPTH_CONVERSION_H_
#define ZIMG_DEPTH_DEPTH_CONVERSION_H_

#include <cstdint>

namespace zimg {;

enum class CPUClass;

template <class T>
class ImageTile;

namespace depth {;

/**
 * Base class for non-dithering conversions.
 */
class DepthConvert {
public:
	/**
	 * Destroy implementation.
	 */
	virtual ~DepthConvert() = 0;

	/**
	 * Convert from byte to half precision.
	 *
	 * @param src input tile
	 * @param dst output tile
	 */
	virtual void byte_to_half(const ImageTile<const uint8_t> &src, const ImageTile<uint16_t> &dst) const = 0;

	/**
	 * Convert from byte to single precision.
	 *
	 * @see DepthConvert::byte_to_half
	 */
	virtual void byte_to_float(const ImageTile<const uint8_t> &src, const ImageTile<float> &dst) const = 0;

	/**
	 * Convert from word to half precision.
	 *
	 * @see DepthConvert::byte_to_half
	 */
	virtual void word_to_half(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst) const = 0;

	/**
	 * Convert from word to single precision.
	 *
	 * @see DepthConvert::byte_to_half
	 */
	virtual void word_to_float(const ImageTile<const uint16_t> &src, const ImageTile<float> &dst) const = 0;

	/**
	 * Convert from half precision to full precision.
	 *
	 * @param src input samples
	 * @param dst output samples
	 * @param width number of samples
	 */
	virtual void half_to_float(const ImageTile<const uint16_t> &src, const ImageTile<float> &dst) const = 0;

	/**
	 * Convert from single precision to half precision.
	 *
	 * @see DepthConvert::half_to_float
	 */
	virtual void float_to_half(const ImageTile<const float> &src, const ImageTile<uint16_t> &dst) const = 0;
};

/**
 * Create a concrete DepthConvert.
 *
 * @param cpu create implementation optimized for given cpu
 */
DepthConvert *create_depth_convert(CPUClass cpu);

} // namespace depth
} // namespace zimg

#endif // ZIMG_DEPTH_DEPTH_CONVERSION_H_

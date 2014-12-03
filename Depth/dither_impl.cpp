#include <algorithm>
#include <cmath>
#include <random>
#include <utility>
#include <vector>
#include "Common/align.h"
#include "Common/except.h"
#include "Common/tile.h"
#include "depth.h"
#include "dither_impl.h"
#include "dither_impl_x86.h"
#include "quantize.h"

namespace zimg {;
namespace depth {;

namespace {;

static const unsigned short ORDERED_DITHERS[64] = {
	 1, 49, 13, 61,  4, 52, 16, 64,
	33, 17, 45, 29, 36, 20, 48, 32,
	 9, 57,  5, 53, 12, 60,  8, 56,
	41, 25, 37, 21, 44, 28, 40, 24,
	 3, 51, 15, 63,  2, 50, 14, 62,
	35, 19, 47, 31, 34, 18, 46, 30,
	11, 59,  7, 55, 10, 58,  6, 54,
	43, 27, 39, 23, 42, 26, 38, 22	
};

static const int ORDERED_DITHERS_SCALE = 65;

template <class T>
float normalize_dither(T x, T min, T max)
{
	return static_cast<float>(x - min) / static_cast<float>(max) - 0.5f;
}

void get_none_dithers(float *p)
{
	std::fill_n(p, OrderedDither::NUM_DITHERS, 0.0f);
}

void get_ordered_dithers(float *p)
{
	for (int i = 0; i < OrderedDither::NUM_DITHERS_H; ++i) {
		for (int j = 0; j < OrderedDither::NUM_DITHERS_V; ++j) {
			*p++ = normalize_dither((int)ORDERED_DITHERS[(i % 8) * 8 + j % 8], 0, ORDERED_DITHERS_SCALE);
		}
	}
}

void get_random_dithers(float *p)
{
	std::mt19937 mt;
	uint_fast32_t mt_min = std::mt19937::min();
	uint_fast32_t mt_max = std::mt19937::max();

	// Dividing the random numbers by 4 chosen arbitrarily to limit noisiness.
	std::generate_n(p, OrderedDither::NUM_DITHERS, [&]() { return normalize_dither(mt(), mt_min, mt_max) * 0.25; });
}

class OrderedDitherC : public OrderedDither {
	template <class T, class U, class ToFloat, class FromFloat>
	void dither(const ImageTile<const T> &src, const ImageTile<U> &dst, ToFloat to_float, FromFloat from_float) const
	{
		const float *dither_data = m_dither.data();
		int depth = dst.descriptor()->format.depth;

		float scale = 1.0f / (float)(1 << (depth - 1));
		auto dither_pixel = [=](T x, float d) { return from_float(to_float(x) + d * scale); };

		for (int i = 0; i < src.height(); ++i) {
			const float *dither_row = &dither_data[(i % NUM_DITHERS_V) * NUM_DITHERS_H];

			for (int j = 0; j < src.width(); ++j) {
				dst[i][j] = dither_pixel(src[i][j], dither_row[j % NUM_DITHERS_H]);
			}
		}
	}
public:
	explicit OrderedDitherC(const float *dither) : OrderedDither(dither)
	{}

	void byte_to_byte(const ImageTile<const uint8_t> &src, const ImageTile<uint8_t> &dst, float *tmp) const override
	{
		dither(src, dst, make_integer_to_float<uint8_t>(src.descriptor()->format), make_float_to_integer<uint8_t>(dst.descriptor()->format));
	}

	void byte_to_word(const ImageTile<const uint8_t> &src, const ImageTile<uint16_t> &dst, float *tmp) const override
	{
		dither(src, dst, make_integer_to_float<uint8_t>(src.descriptor()->format), make_float_to_integer<uint16_t>(dst.descriptor()->format));
	}

	void word_to_byte(const ImageTile<const uint16_t> &src, const ImageTile<uint8_t> &dst, float *tmp) const override
	{
		dither(src, dst, make_integer_to_float<uint16_t>(src.descriptor()->format), make_float_to_integer<uint8_t>(dst.descriptor()->format));
	}

	void word_to_word(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, float *tmp) const override
	{
		dither(src, dst, make_integer_to_float<uint16_t>(src.descriptor()->format), make_float_to_integer<uint16_t>(dst.descriptor()->format));
	}

	void half_to_byte(const ImageTile<const uint16_t> &src, const ImageTile<uint8_t> &dst, float *tmp) const override
	{
		dither(src, dst, depth::half_to_float, make_float_to_integer<uint8_t>(dst.descriptor()->format));
	}

	void half_to_word(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, float *tmp) const override
	{
		dither(src, dst, depth::half_to_float, make_float_to_integer<uint16_t>(dst.descriptor()->format));
	}

	void float_to_byte(const ImageTile<const float> &src, const ImageTile<uint8_t> &dst, float *tmp) const override
	{
		dither(src, dst, identity<float>, make_float_to_integer<uint8_t>(dst.descriptor()->format));
	}

	void float_to_word(const ImageTile<const float> &src, const ImageTile<uint16_t> &dst, float *tmp) const override
	{
		dither(src, dst, identity<float>, make_float_to_integer<uint16_t>(dst.descriptor()->format));
	}
};

} // namespace


OrderedDither::OrderedDither(const float *dither) :
	m_dither(dither, dither + NUM_DITHERS)
{
}

DitherConvert *create_ordered_dither(DitherType type, CPUClass cpu)
{
	DitherConvert *ret = nullptr;
	float dither[OrderedDither::NUM_DITHERS];

	switch (type) {
	case DitherType::DITHER_NONE:
		get_none_dithers(dither);
		break;
	case DitherType::DITHER_ORDERED:
		get_ordered_dithers(dither);
		break;
	case DitherType::DITHER_RANDOM:
		get_random_dithers(dither);
		break;
	default:
		throw ZimgIllegalArgument{ "unrecognized ordered dither type" };
	}

#ifdef ZIMG_X86
	ret = create_ordered_dither_x86(dither, cpu);
#endif
	if (!ret)
		ret = new OrderedDitherC{ dither };

	return ret;
}

} // namespace depth
} // namespace zimg

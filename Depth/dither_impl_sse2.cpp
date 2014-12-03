#ifdef ZIMG_X86

#include <emmintrin.h>
#include "Common/tile.h"
#include "dither_impl.h"
#include "dither_impl_x86.h"
#include "quantize_sse2.h"

namespace zimg {;
namespace depth {;

namespace {;

struct DitherPolicySSE2 {
	typedef __m128 type;
	static const int vector_size = 4;

	__m128 set1(float x) { return _mm_set_ps1(x); }

	__m128 load(const float *ptr) { return _mm_load_ps(ptr); }

	__m128 add(__m128 a, __m128 b) { return _mm_add_ps(a, b); }

	__m128 mul(__m128 a, __m128 b) { return _mm_mul_ps(a, b); }
};

class OrderedDitherSSE2 : public OrderedDitherX86 {
public:
	explicit OrderedDitherSSE2(const float *dither) : OrderedDitherX86(dither)
	{}

	void byte_to_byte(const ImageTile<const uint8_t> &src, const ImageTile<uint8_t> &dst, float *tmp) const override
	{
		process(src, dst, DitherPolicySSE2{}, UnpackByteSSE2{}, PackByteSSE2{},
		        make_integer_to_float_sse2(src.descriptor()->format), make_float_to_integer_sse2(dst.descriptor()->format),
		        make_integer_to_float<uint8_t>(src.descriptor()->format), make_float_to_integer<uint8_t>(dst.descriptor()->format));
	}

	void byte_to_word(const ImageTile<const uint8_t> &src, const ImageTile<uint16_t> &dst, float *tmp) const override
	{
		process(src, dst, DitherPolicySSE2{}, UnpackByteSSE2{}, PackWordSSE2{},
		        make_integer_to_float_sse2(src.descriptor()->format), make_float_to_integer_sse2(dst.descriptor()->format),
		        make_integer_to_float<uint8_t>(src.descriptor()->format), make_float_to_integer<uint16_t>(dst.descriptor()->format));
	}

	void word_to_byte(const ImageTile<const uint16_t> &src, const ImageTile<uint8_t> &dst, float *tmp) const override
	{
		process(src, dst, DitherPolicySSE2{}, UnpackWordSSE2{}, PackByteSSE2{},
		        make_integer_to_float_sse2(src.descriptor()->format), make_float_to_integer_sse2(dst.descriptor()->format),
		        make_integer_to_float<uint16_t>(src.descriptor()->format), make_float_to_integer<uint8_t>(dst.descriptor()->format));
	}

	void word_to_word(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, float *tmp) const override
	{
		process(src, dst, DitherPolicySSE2{}, UnpackWordSSE2{}, PackWordSSE2{},
		        make_integer_to_float_sse2(src.descriptor()->format), make_float_to_integer_sse2(dst.descriptor()->format),
		        make_integer_to_float<uint16_t>(src.descriptor()->format), make_float_to_integer<uint16_t>(dst.descriptor()->format));
	}

	void half_to_byte(const ImageTile<const uint16_t> &src, const ImageTile<uint8_t> &dst, float *tmp) const override
	{
		process<uint16_t, uint8_t>(src, dst, DitherPolicySSE2{}, UnpackWordSSE2{}, PackByteSSE2{},
		        half_to_float_sse2, make_float_to_integer_sse2(dst.descriptor()->format),
		        depth::half_to_float, make_float_to_integer<uint8_t>(dst.descriptor()->format));
	}

	void half_to_word(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, float *tmp) const override
	{
		process(src, dst, DitherPolicySSE2{}, UnpackWordSSE2{}, PackWordSSE2{},
		        half_to_float_sse2, make_float_to_integer_sse2(dst.descriptor()->format),
		        depth::half_to_float, make_float_to_integer<uint16_t>(dst.descriptor()->format));
	}

	void float_to_byte(const ImageTile<const float> &src, const ImageTile<uint8_t> &dst, float *tmp) const override
	{
		process(src, dst, DitherPolicySSE2{}, UnpackFloatSSE2{}, PackByteSSE2{},
		        identity<__m128>, make_float_to_integer_sse2(dst.descriptor()->format),
		        identity<float>, make_float_to_integer<uint8_t>(dst.descriptor()->format));
	}

	void float_to_word(const ImageTile<const float> &src, const ImageTile<uint16_t> &dst, float *tmp) const override
	{
		process(src, dst, DitherPolicySSE2{}, UnpackFloatSSE2{}, PackWordSSE2{},
		        identity<__m128>, make_float_to_integer_sse2(dst.descriptor()->format),
		        identity<float>, make_float_to_integer<uint16_t>(dst.descriptor()->format));
	}
};

} // namespace


DitherConvert *create_ordered_dither_sse2(const float *dither)
{
	return new OrderedDitherSSE2{ dither };
}

}
}

#endif // ZIMG_X86

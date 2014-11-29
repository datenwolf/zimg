#ifdef ZIMG_X86

#include <immintrin.h>
#include "Common/tile.h"
#include "depth_convert.h"
#include "depth_convert_x86.h"
#include "quantize.h"
#include "quantize_avx2.h"

namespace zimg {;
namespace depth {;

namespace {;

class DepthConvertAVX2 : public DepthConvertX86 {
public:
	void byte_to_half(const ImageTile &src, const ImageTile &dst) const override
	{
		auto cvt_avx2 = make_integer_to_float_avx2(src.format);
		auto cvt = make_integer_to_float<uint8_t>(src.format);

		process<uint8_t, uint16_t>(src, dst, UnpackByteAVX2{}, PackHalfAVX2{},
		                           [=](__m256i x) { return float_to_half_avx2(cvt_avx2(x)); },
		                           [=](uint8_t x) { return depth::float_to_half(cvt(x)); });
	}

	void byte_to_float(const ImageTile &src, const ImageTile &dst) const override
	{
		auto cvt_avx2 = make_integer_to_float_avx2(src.format);
		auto cvt = make_integer_to_float<uint8_t>(src.format);

		process<uint8_t, float>(src, dst, UnpackByteAVX2{}, PackFloatAVX2{}, cvt_avx2, cvt);
	}

	void word_to_half(const ImageTile &src, const ImageTile &dst) const override
	{
		auto cvt_avx2 = make_integer_to_float_avx2(src.format);
		auto cvt = make_integer_to_float<uint16_t>(src.format);

		process<uint16_t, uint16_t>(src, dst, UnpackWordAVX2{}, PackHalfAVX2{},
		                            [=](__m256i x) { return float_to_half_avx2(cvt_avx2(x)); },
		                            [=](uint16_t x) { return depth::float_to_half(cvt(x)); });
	}

	void word_to_float(const ImageTile &src, const ImageTile &dst) const override
	{
		auto cvt_avx2 = make_integer_to_float_avx2(src.format);
		auto cvt = make_integer_to_float<uint16_t>(src.format);

		process<uint16_t, float>(src, dst, UnpackWordAVX2{}, PackFloatAVX2{}, cvt_avx2, cvt);
	}

	void half_to_float(const ImageTile &src, const ImageTile &dst) const override
	{
		process<uint16_t, float>(src, dst, UnpackHalfAVX2{}, PackFloatAVX2{}, half_to_float_avx2, depth::half_to_float);
	}

	void float_to_half(const ImageTile &src, const ImageTile &dst) const override
	{
		process<float, uint16_t>(src, dst, UnpackFloatAVX2{}, PackHalfAVX2{}, float_to_half_avx2, depth::float_to_half);
	}
};

} // namespace


DepthConvert *create_depth_convert_avx2()
{
	return new DepthConvertAVX2{};
}

} // namespace depth
} // namespace zimg

#endif // ZIMG_X86

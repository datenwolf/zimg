#include <cstring>
#include "Common/cpuinfo.h"
#include "Common/tile.h"
#include "depth_convert.h"
#include "depth_convert_x86.h"
#include "quantize.h"

namespace zimg {;
namespace depth {;

namespace {;

class DepthConvertC : public DepthConvert {
	template <class T, class U, class Proc>
	void process_tile(const ImageTile<const T> &src, const ImageTile<U> &dst, Proc proc) const
	{
		for (int i = 0; i < src.height(); ++i) {
			for (int j = 0; j < src.width(); ++j) {
				dst[i][j] = proc(src[i][j]);
			}
		}
	}
public:
	void byte_to_half(const ImageTile<const uint8_t> &src, const ImageTile<uint16_t> &dst) const override
	{
		auto cvt = make_integer_to_float<uint8_t>(src.descriptor()->format);
		process_tile(src, dst, [=](uint8_t x) { return depth::float_to_half(cvt(x)); });
	}

	void byte_to_float(const ImageTile<const uint8_t> &src, const ImageTile<float> &dst) const override
	{
		process_tile(src, dst, make_integer_to_float<uint8_t>(src.descriptor()->format));
	}

	void word_to_half(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst) const override
	{
		auto cvt = make_integer_to_float<uint16_t>(src.descriptor()->format);
		process_tile<uint16_t, uint16_t>(src, dst, [=](uint16_t x) { return depth::float_to_half(cvt(x)); });
	}

	void word_to_float(const ImageTile<const uint16_t> &src, const ImageTile<float> &dst) const override
	{
		process_tile<uint16_t, float>(src, dst, make_integer_to_float<uint16_t>(src.descriptor()->format));
	}

	void half_to_float(const ImageTile<const uint16_t> &src, const ImageTile<float> &dst) const override
	{
		process_tile(src, dst, depth::half_to_float);
	}

	void float_to_half(const ImageTile<const float> &src, const ImageTile<uint16_t> &dst) const override
	{
		process_tile(src, dst, depth::float_to_half);
	}
};

} // namespace


DepthConvert::~DepthConvert()
{
}

DepthConvert *create_depth_convert(CPUClass cpu)
{
	DepthConvert *ret = nullptr;

#ifdef ZIMG_X86
	ret = create_depth_convert_x86(cpu);
#endif
	if (!ret)
		ret = new DepthConvertC{};

	return ret;
}

} // namespace depth
} // namespace zimg

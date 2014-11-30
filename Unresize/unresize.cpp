#include <algorithm>
#include "Common/align.h"
#include "Common/cpuinfo.h"
#include "Common/except.h"
#include "Common/pixel.h"
#include "Common/tile.h"
#include "unresize.h"
#include "unresize_impl.h"

namespace zimg {;
namespace unresize {;

Unresize::Unresize(bool horizontal, int src_dim, int dst_dim, double shift, CPUClass cpu) try :
	m_impl{ create_unresize_impl(horizontal, src_dim, dst_dim, shift, cpu) },
	m_dst_dim{ dst_dim },
	m_horizontal{ horizontal }
{
}
catch (const std::bad_alloc &) {
	throw ZimgOutOfMemory{};
}

Unresize::~Unresize() 
{
}

size_t Unresize::tmp_size(PixelType type) const
{
	size_t size = 0;

	// Line buffer for horizontal pass.
	if (m_horizontal)
		size += (size_t)m_dst_dim * 8;

	return size;
}

void Unresize::process(const ImageTile &src, const ImageTile &dst, void *tmp) const
{
	switch (src.format.type) {
	case PixelType::HALF:
		m_impl->process_f16(src, dst, tmp);
		break;
	case PixelType::FLOAT:
		m_impl->process_f32(src, dst, tmp);
		break;
	default:
		throw ZimgUnsupportedError{ "only HALF and FLOAT supported for unresize" };
	}
}

bool unresize_horizontal_first(double xscale, double yscale)
{
	// Downscaling cost is proportional to input size, whereas upscaling cost is proportional to output size.
	// Horizontal operation is roughly twice as costly as vertical operation for SIMD cores.
	double h_first_cost = std::max(xscale, 1.0) * 2.0 + xscale * std::max(yscale, 1.0);
	double v_first_cost = std::max(yscale, 1.0) + yscale * std::max(xscale, 1.0) * 2.0;

	return h_first_cost < v_first_cost;
}

} // namespace unresize
} // namespace zimg

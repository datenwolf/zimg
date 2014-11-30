#include <algorithm>
#include <cstddef>
#include "Common/except.h"
#include "Common/pixel.h"
#include "Common/tile.h"
#include "resize.h"
#include "resize_impl.h"

namespace zimg {;
namespace resize {;

Resize::Resize(const Filter &f, bool horizontal, int src_dim, int dst_dim, double shift, double width, CPUClass cpu)
try :
	m_impl{ create_resize_impl(f, horizontal, src_dim, dst_dim, shift, width, cpu) },
	m_horizontal{ horizontal }
{
} catch (const std::bad_alloc &) {
	throw ZimgOutOfMemory{};
}

size_t Resize::tmp_size(PixelType type, int width) const
{
	size_t size = 0;

	// Need a line buffer to store cached accumulators.
	if (!m_horizontal && type == PixelType::WORD)
		size += (size_t)width * 4;

	return size;
}

void Resize::dependent_rect(int dst_top, int dst_left, int dst_bottom, int dst_right, int *src_top, int *src_left, int *src_bottom, int *src_right) const
{
	m_impl->dependent_rect(dst_top, dst_left, dst_bottom, dst_right, src_top, src_left, src_bottom, src_right);
}

void Resize::process(const ImageTile &src, const ImageTile &dst, int i, int j, void *tmp) const
{
	switch (src.format.type) {
	case PixelType::WORD:
		m_impl->process_u16(src, dst, i, j, tmp);
		break;
	case PixelType::HALF:
		m_impl->process_f16(src, dst, i, j, tmp);
		break;
	case PixelType::FLOAT:
		m_impl->process_f32(src, dst, i, j, tmp);
		break;
	default:
		throw ZimgUnsupportedError{ "only WORD, HALF, and FLOAT are supported for resize" };
	}
}

bool resize_horizontal_first(double xscale, double yscale)
{
	// Downscaling cost is proportional to input size, whereas upscaling cost is proportional to output size.
	// Horizontal operation is roughly twice as costly as vertical operation for SIMD cores.
	double h_first_cost = std::max(xscale, 1.0) * 2.0 + xscale * std::max(yscale, 1.0);
	double v_first_cost = std::max(yscale, 1.0)       + yscale * std::max(xscale, 1.0) * 2.0;

	return h_first_cost < v_first_cost;
}

} // namespace resize
} // namespace zimg

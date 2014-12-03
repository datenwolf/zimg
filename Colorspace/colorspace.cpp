#include <algorithm>
#include <functional>
#include <unordered_map>
#include <vector>
#include "Common/align.h"
#include "Common/except.h"
#include "Common/pixel.h"
#include "Common/tile.h"
#include "colorspace.h"
#include "colorspace_param.h"
#include "graph.h"

namespace zimg {;
namespace colorspace {;

namespace {;

bool is_valid_csp(const ColorspaceDefinition &csp)
{
	return !(csp.matrix == MatrixCoefficients::MATRIX_2020_CL && csp.transfer == TransferCharacteristics::TRANSFER_LINEAR);
}

} // namespace


ColorspaceConversion::ColorspaceConversion(const ColorspaceDefinition &in, const ColorspaceDefinition &out, CPUClass cpu)
try :
	m_pixel_adapter{ create_pixel_adapter(cpu) }
{	
	if (!is_valid_csp(in) || !is_valid_csp(out))
		throw ZimgIllegalArgument{ "invalid colorspace definition" };

	for (const auto &func : get_operation_path(in, out)) {
		m_operations.emplace_back(func(cpu));
	}
} catch (const std::bad_alloc &) {
	throw ZimgOutOfMemory{};
}

void ColorspaceConversion::load_tile(const ImageTile<const void> &src, float *dst) const
{
	PlaneDescriptor desc{ PixelType::FLOAT };
	ImageTile<float> dst_tile{ dst, &desc, ceil_n(src.width() * (int)sizeof(float), ALIGNMENT), src.width(), src.height() };

	switch (src.descriptor()->format.type) {
	case PixelType::HALF:
		m_pixel_adapter->f16_to_f32(tile_cast<const uint16_t>(src), dst_tile);
		break;
	case PixelType::FLOAT:
		copy_image_tile(tile_cast<const float>(src), dst_tile);
		break;
	default:
		break;
	}
}

void ColorspaceConversion::store_tile(const float *src, const ImageTile<void> &dst) const
{
	PlaneDescriptor desc{ PixelType::FLOAT };
	ImageTile<const float> src_tile{ src, &desc, ceil_n(dst.width() * (int)sizeof(float), ALIGNMENT), dst.width(), dst.height() };

	switch (dst.descriptor()->format.type) {
	case PixelType::HALF:
		m_pixel_adapter->f32_to_f16(src_tile, tile_cast<uint16_t>(dst));
		break;
	case PixelType::FLOAT:
		copy_image_tile(src_tile, tile_cast<float>(dst));
		break;
	default:
		break;
	}
}

bool ColorspaceConversion::pixel_supported(PixelType type) const
{
	return (m_pixel_adapter && type == PixelType::HALF) || type == PixelType::FLOAT;
}

size_t ColorspaceConversion::tmp_size(int width, int height) const
{
	size_t stride = ceil_n(width, AlignmentOf<float>::value);
	return 3 * stride * height;
}

void ColorspaceConversion::process_tile(const ImageTile<const void> src[3], const ImageTile<void> dst[3], void *tmp) const
{
	int tmp_tile_size = ceil_n(src[0].width(), AlignmentOf<float>::value) * src[0].height();
	float *tmp_ptr[3];

	tmp_ptr[0] = (float *)tmp + 0 * tmp_tile_size;
	tmp_ptr[1] = (float *)tmp + 1 * tmp_tile_size;
	tmp_ptr[2] = (float *)tmp + 2 * tmp_tile_size;

	load_tile(src[0], tmp_ptr[0]);
	load_tile(src[1], tmp_ptr[1]);
	load_tile(src[2], tmp_ptr[2]);

	for (const auto &op : m_operations) {
		op->process(tmp_ptr, tmp_tile_size);
	}

	store_tile(tmp_ptr[0], dst[0]);
	store_tile(tmp_ptr[1], dst[1]);
	store_tile(tmp_ptr[2], dst[2]);
}

} // namespace colorspace
} // namespace zimg

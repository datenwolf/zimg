#include <cstddef>
#include <utility>
#include "Common/except.h"
#include "Common/pixel.h"
#include "Common/tile.h"
#include "depth.h"
#include "depth_convert.h"
#include "dither.h"

namespace zimg {;
namespace depth {;

namespace {;

void convert_dithered(const DitherConvert &dither, const ImageTile<const void> &src, const ImageTile<void> &dst, void *tmp)
{
	PixelType src_type = src.descriptor()->format.type;
	PixelType dst_type = dst.descriptor()->format.type;
	float *tmp_f = (float *)tmp;

	if (dst_type == PixelType::BYTE) {
		ImageTile<uint8_t> dst_b = tile_cast<uint8_t>(dst);

		switch (src_type) {
		case PixelType::BYTE:
			dither.byte_to_byte(tile_cast<const uint8_t>(src), dst_b, tmp_f);
			break;
		case PixelType::WORD:
			dither.word_to_byte(tile_cast<const uint16_t>(src), dst_b, tmp_f);
			break;
		case PixelType::HALF:
			dither.half_to_byte(tile_cast<const uint16_t>(src), dst_b, tmp_f);
			break;
		case PixelType::FLOAT:
			dither.float_to_byte(tile_cast<const float>(src), dst_b, tmp_f);
			break;
		}
	} else if (dst_type == PixelType::WORD) {
		ImageTile<uint16_t> dst_w = tile_cast<uint16_t>(dst);

		switch (src_type) {
		case PixelType::BYTE:
			dither.byte_to_word(tile_cast<const uint8_t>(src), dst_w, tmp_f);
			break;
		case PixelType::WORD:
			dither.word_to_word(tile_cast<const uint16_t>(src), dst_w, tmp_f);
			break;
		case PixelType::HALF:
			dither.half_to_word(tile_cast<const uint16_t>(src), dst_w, tmp_f);
			break;
		case PixelType::FLOAT:
			dither.float_to_word(tile_cast<const float>(src), dst_w, tmp_f);
			break;
		}
	}
}

void convert_depth(const DepthConvert &depth, const ImageTile<const void> &src, const ImageTile<void> &dst)
{
	PixelType src_type = src.descriptor()->format.type;
	PixelType dst_type = dst.descriptor()->format.type;

	if (dst_type == PixelType::HALF) {
		ImageTile<uint16_t> dst_w = tile_cast<uint16_t>(dst);

		switch (src_type) {
		case PixelType::BYTE:
			depth.byte_to_half(tile_cast<const uint8_t>(src), dst_w);
			break;
		case PixelType::WORD:
			depth.word_to_half(tile_cast<const uint16_t>(src), dst_w);
			break;
		case PixelType::HALF:
			copy_image_tile(tile_cast<const uint16_t>(src), dst_w);
			break;
		case PixelType::FLOAT:
			depth.float_to_half(tile_cast<const float>(src), dst_w);
			break;
		}
	} else if (dst_type == PixelType::FLOAT) {
		ImageTile<float> dst_f = tile_cast<float>(dst);

		switch (src_type) {
		case PixelType::BYTE:
			depth.byte_to_float(tile_cast<const uint8_t>(src), dst_f);
			break;
		case PixelType::WORD:
			depth.word_to_float(tile_cast<const uint16_t>(src), dst_f);
			break;
		case PixelType::HALF:
			depth.half_to_float(tile_cast<const uint16_t>(src), dst_f);
			break;
		case PixelType::FLOAT:
			copy_image_tile(tile_cast<const float>(src), dst_f);
			break;
		}
	}
}

} // namespace


Depth::Depth(DitherType type, CPUClass cpu) try :
	m_depth{ create_depth_convert(cpu) },
	m_dither{ create_dither_convert(type, cpu) },
	m_error_diffusion{ type == DitherType::DITHER_ERROR_DIFFUSION }
{
}
catch (const std::bad_alloc &)
{
	throw ZimgOutOfMemory{};
}

bool Depth::tile_supported(PixelType src_type, PixelType dst_type) const
{
	return dst_type == PixelType::HALF || dst_type == PixelType::WORD || !m_error_diffusion;
}

size_t Depth::tmp_size(int width) const
{
	return m_error_diffusion ? ((size_t)width + 2) * 2 : 0;
}

void Depth::process_tile(const ImageTile<const void> &src, const ImageTile<void> &dst, void *tmp) const
{
	if (dst.descriptor()->format.type >= PixelType::HALF)
		convert_depth(*m_depth, src, dst);
	else
		convert_dithered(*m_dither, src, dst, tmp);
}

} // namespace depth
} // namespace zimg

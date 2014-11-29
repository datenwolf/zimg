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

void convert_dithered(const DitherConvert &dither, const ImageTile &src, const ImageTile &dst, void *tmp)
{
	PixelType src_type = src.format.type;
	PixelType dst_type = dst.format.type;
	float *tmp_f = (float *)tmp;

	if (src_type == PixelType::BYTE && dst_type == PixelType::BYTE)
		dither.byte_to_byte(src, dst, tmp_f);
	else if (src_type == PixelType::BYTE && dst_type == PixelType::WORD)
		dither.byte_to_word(src, dst, tmp_f);
	else if (src_type == PixelType::WORD && dst_type == PixelType::BYTE)
		dither.word_to_byte(src, dst, tmp_f);
	else if (src_type == PixelType::WORD && dst_type == PixelType::WORD)
		dither.word_to_word(src, dst, tmp_f);
	else if (src_type == PixelType::HALF && dst_type == PixelType::BYTE)
		dither.half_to_byte(src, dst, tmp_f);
	else if (src_type == PixelType::HALF && dst_type == PixelType::WORD)
		dither.half_to_word(src, dst, tmp_f);
	else if (src_type == PixelType::FLOAT && dst_type == PixelType::BYTE)
		dither.float_to_byte(src, dst, tmp_f);
	else if (src_type == PixelType::FLOAT && dst_type == PixelType::WORD)
		dither.float_to_word(src, dst, tmp_f);
}

void convert_depth(const DepthConvert &depth, const ImageTile &src, const ImageTile &dst)
{
	PixelType src_type = src.format.type;
	PixelType dst_type = dst.format.type;

	if (src_type == PixelType::BYTE && dst_type == PixelType::HALF)
		depth.byte_to_half(src, dst);
	else if (src_type == PixelType::BYTE && dst_type == PixelType::FLOAT)
		depth.byte_to_float(src, dst);
	else if (src_type == PixelType::WORD && dst_type == PixelType::HALF)
		depth.word_to_half(src, dst);
	else if (src_type == PixelType::WORD && dst_type == PixelType::FLOAT)
		depth.word_to_float(src, dst);
	else if (src_type == PixelType::HALF && dst_type == PixelType::FLOAT)
		depth.half_to_float(src, dst);
	else if (src_type == PixelType::FLOAT && dst_type == PixelType::HALF)
		depth.float_to_half(src, dst);
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

void Depth::process_tile(const ImageTile &src, const ImageTile &dst, void *tmp) const
{
	if (dst.format.type >= PixelType::HALF)
		convert_depth(*m_depth, src, dst);
	else
		convert_dithered(*m_dither, src, dst, tmp);
}

} // namespace depth
} // namespace zimg

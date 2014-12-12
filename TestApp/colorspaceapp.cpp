#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include "Common/cpuinfo.h"
#include "Common/pixel.h"
#include "Common/tile.h"
#include "Colorspace/colorspace.h"
#include "Colorspace/colorspace_param.h"
#include "apps.h"
#include "frame.h"
#include "utils.h"

using namespace zimg;

namespace {;

struct AppContext {
	const char *infile;
	const char *outfile;
	int width;
	int height;
	colorspace::ColorspaceDefinition csp_in;
	colorspace::ColorspaceDefinition csp_out;
	int fullrange_in;
	int fullrange_out;
	const char *visualise;
	int times;
	CPUClass cpu;
	PixelType filetype;
	PixelType pixtype;
};

const AppOption OPTIONS[] = {
	{ "tv-in",     OptionType::OPTION_FALSE,     offsetof(AppContext, fullrange_in) },
	{ "pc-in",     OptionType::OPTION_TRUE,      offsetof(AppContext, fullrange_in) },
	{ "tv-out",    OptionType::OPTION_FALSE,     offsetof(AppContext, fullrange_out) },
	{ "pc-out",    OptionType::OPTION_TRUE,      offsetof(AppContext, fullrange_out) },
	{ "visualise", OptionType::OPTION_STRING,    offsetof(AppContext, visualise) },
	{ "times",     OptionType::OPTION_INTEGER,   offsetof(AppContext, times) },
	{ "cpu",       OptionType::OPTION_CPUCLASS,  offsetof(AppContext, cpu) },
    { "filetype",  OptionType::OPTION_PIXELTYPE, offsetof(AppContext, filetype) },
	{ "pixtype",   OptionType::OPTION_PIXELTYPE, offsetof(AppContext, pixtype) }
};

void usage()
{
	std::cout << "colorspace infile outfile w h csp_in csp_out [--tv-in | --pc-in] [--tv-out | --pc-out] [--visualise path] [--times n] [--cpu cpu] [--pixtype type]\n";
	std::cout << "    infile               input file\n";
	std::cout << "    outfile              output file\n";
	std::cout << "    w                    image width\n";
	std::cout << "    h                    image height\n";
	std::cout << "    csp_in               input colorspace\n";
	std::cout << "    csp_out              output colorspace\n";
	std::cout << "    --tv-in | --pc-in    toggle TV vs PC range for input\n";
	std::cout << "    --tv-out | --pc-out  toggle TV vs PC range for output\n";
	std::cout << "    --visualise          path to BMP file for visualisation\n";
	std::cout << "    --times              number of cycles\n";
	std::cout << "    --cpu                select CPU type\n";
	std::cout << "    --filetype           pixel format of input/output files\n";
	std::cout << "    --pixtype            select working pixel format\n";
}

colorspace::MatrixCoefficients parse_matrix(const char *matrix)
{
	if (!strcmp(matrix, "rgb"))
		return colorspace::MatrixCoefficients::MATRIX_RGB;
	else if (!strcmp(matrix, "601"))
		return colorspace::MatrixCoefficients::MATRIX_601;
	else if (!strcmp(matrix, "709"))
		return colorspace::MatrixCoefficients::MATRIX_709;
	else if (!strcmp(matrix, "2020_ncl"))
		return colorspace::MatrixCoefficients::MATRIX_2020_NCL;
	else if (!strcmp(matrix, "2020_cl"))
		return colorspace::MatrixCoefficients::MATRIX_2020_CL;
	else
		throw std::runtime_error{ "bad matrix coefficients" };
}

colorspace::TransferCharacteristics parse_transfer(const char *transfer)
{
	if (!strcmp(transfer, "linear"))
		return colorspace::TransferCharacteristics::TRANSFER_LINEAR;
	else if (!strcmp(transfer, "709"))
		return colorspace::TransferCharacteristics::TRANSFER_709;
	else
		throw std::runtime_error{ "bad transfer characteristics" };
}

colorspace::ColorPrimaries parse_primaries(const char *primaries)
{
	if (!strcmp(primaries, "smpte_c"))
		return colorspace::ColorPrimaries::PRIMARIES_SMPTE_C;
	else if (!strcmp(primaries, "709"))
		return colorspace::ColorPrimaries::PRIMARIES_709;
	else if (!strcmp(primaries, "2020"))
		return colorspace::ColorPrimaries::PRIMARIES_2020;
	else
		throw std::runtime_error{ "bad primaries" };
}

colorspace::ColorspaceDefinition parse_csp(const char *str)
{
	colorspace::ColorspaceDefinition csp;
	std::string s{ str };
	std::string sub;
	size_t prev;
	size_t curr;

	prev = 0;
	curr = s.find(':');
	if (curr == std::string::npos || curr == s.size() - 1)
		throw std::runtime_error{ "bad colorspace string" };

	sub = s.substr(prev, curr - prev);
	csp.matrix = parse_matrix(sub.c_str());

	prev = curr + 1;
	curr = s.find(':', prev);
	if (curr == std::string::npos || curr == s.size() - 1)
		throw std::runtime_error{ "bad colorspace string" };

	sub = s.substr(prev, curr - prev);
	csp.transfer = parse_transfer(sub.c_str());

	prev = curr + 1;
	curr = s.size();
	sub = s.substr(prev, curr - prev);
	csp.primaries = parse_primaries(sub.c_str());

	return csp;
}

void execute(const colorspace::ColorspaceConversion &conv, const Frame &in, Frame &out, int times,
             bool fullrange_in, bool fullrange_out, bool yuv_in, bool yuv_out, PixelType filetype, PixelType type)
{
	int width = in.width();
	int height = in.height();

	Frame in_conv{ width, height, pixel_size(type), 3 };
	Frame out_conv{ width, height, pixel_size(type), 3 };

	int in_byte_stride = in_conv.stride() * in_conv.pxsize();
	int out_byte_stride = out_conv.stride() * out_conv.pxsize();

	auto tmp = allocate_buffer(conv.tmp_size(), PixelType::FLOAT);

	convert_frame(in, in_conv, filetype, type, fullrange_in, yuv_in);

	measure_time(times, [&]()
	{
		PlaneDescriptor desc{ type };

		ImageTile<const void> in_frame_tiles[3];
		ImageTile<void> out_frame_tiles[3];

		for (int p = 0; p < 3; ++p) {
			in_frame_tiles[p] = ImageTile<const void>{ in_conv.data(p), &desc, in_byte_stride };
			out_frame_tiles[p] = ImageTile<void>{ out_conv.data(p), &desc, out_byte_stride };
		}

		for (int i = 0; i < height; i += TILE_HEIGHT) {
			for (int j = 0; j < width; j += TILE_WIDTH) {
				ImageTile<const void> in_tiles[3];
				ImageTile<void> out_tiles[3];

				for (int p = 0; p < 3; ++p) {
					in_tiles[p] = in_frame_tiles[p].sub_tile(i, j);
					out_tiles[p] = out_frame_tiles[p].sub_tile(i, j);
				}

				conv.process_tile(in_tiles, out_tiles, tmp.data());
			}
		}
	});

	convert_frame(out_conv, out, type, filetype, fullrange_out, yuv_out);
}

} // namespace


int colorspace_main(int argc, const char **argv)
{
	if (argc < 7) {
		usage();
		return -1;
	}

	AppContext c{};

	c.infile           = argv[1];
	c.outfile          = argv[2];
	c.width            = std::stoi(argv[3]);
	c.height           = std::stoi(argv[4]);
	c.csp_in           = parse_csp(argv[5]);
	c.csp_out          = parse_csp(argv[6]);
	c.fullrange_in     = 0;
	c.fullrange_out    = 0;
	c.visualise        = nullptr;
	c.times            = 1;
	c.filetype         = PixelType::FLOAT;
	c.pixtype          = PixelType::FLOAT;
	c.cpu              = CPUClass::CPU_NONE;

	parse_opts(argv + 7, argv + argc, std::begin(OPTIONS), std::end(OPTIONS), &c, nullptr);

	int width = c.width;
	int height = c.height;
	int pxsize = pixel_size(c.filetype);

	bool yuv_in = c.csp_in.matrix != colorspace::MatrixCoefficients::MATRIX_RGB;
	bool yuv_out = c.csp_out.matrix != colorspace::MatrixCoefficients::MATRIX_RGB;

	Frame in{ width, height, pxsize, 3 };
	Frame out{ width, height, pxsize, 3 };

	read_frame_raw(in, c.infile);

	colorspace::ColorspaceConversion conv{ c.csp_in, c.csp_out, c.cpu };
	execute(conv, in, out, c.times, !!c.fullrange_in, !!c.fullrange_out, yuv_in, yuv_out, c.filetype, c.pixtype);

	write_frame_raw(out, c.outfile);

	if (c.visualise) {
		Frame bmp{ width, height, 1, 3 };

		convert_frame(out, bmp, c.filetype, PixelType::BYTE, !!c.fullrange_out, yuv_out);
		write_frame_bmp(bmp, c.visualise);
	}

	return 0;
}

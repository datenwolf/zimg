#include <cstddef>
#include <iostream>
#include <iterator>
#include <string>
#include "Common/cpuinfo.h"
#include "Common/pixel.h"
#include "Common/tile.h"
#include "Unresize/unresize.h"
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
	double shift_w;
	double shift_h;
	int times;
	CPUClass cpu;
	PixelType pixtype;
};

const AppOption OPTIONS[] = {
	{ "shift-w", OptionType::OPTION_FLOAT,     offsetof(AppContext, shift_w) },
	{ "shift-h", OptionType::OPTION_FLOAT,     offsetof(AppContext, shift_h) },
	{ "times",   OptionType::OPTION_INTEGER,   offsetof(AppContext, times) },
	{ "cpu",     OptionType::OPTION_CPUCLASS,  offsetof(AppContext, cpu) },
	{ "pixtype", OptionType::OPTION_PIXELTYPE, offsetof(AppContext, pixtype) }
};

void usage()
{
	std::cout << "unresize infile outfile width height [--shift-w shift] [--shift-h shift] [--times n] [--cpu cpu] [--pixtype type]\n";
	std::cout << "    infile              input BMP file\n";
	std::cout << "    outfile             output BMP file\n";
	std::cout << "    w                   output width\n";
	std::cout << "    h                   output height\n";
	std::cout << "    --shift-w           horizontal shift\n";
	std::cout << "    --shift-h           vertical shift\n";
	std::cout << "    --times             number of cycles\n";
	std::cout << "    --cpu               select CPU type\n";
	std::cout << "    --pixtype           select pixel format\n";
}

void execute(const unresize::Unresize *unresize_h, const unresize::Unresize *unresize_v, const Frame &in, Frame &out, int times, PixelType type)
{
	int pxsize = pixel_size(type);
	int planes = in.planes();

	Frame src{ in.width(), in.height(), pxsize, planes };
	Frame dst{ out.width(), out.height(), pxsize, planes };

	bool hfirst = unresize::unresize_horizontal_first((double)out.width() / in.width(), (double)out.height() / in.height());
	bool skip_h = !unresize_h;
	bool skip_v = !unresize_v;

	int tmp_width = 0;
	int tmp_height = 0;
	size_t tmp_size;

	if (skip_h && skip_v) {
		tmp_size = 0;
	} else if (skip_h && !skip_v) {
		tmp_size = unresize_v->tmp_size(type);
	} else if (skip_v && !skip_h) {
		tmp_size = unresize_h->tmp_size(type);
	} else {
		if (hfirst) {
			tmp_width = out.width();
			tmp_height = in.height();

			tmp_size = std::max(unresize_h->tmp_size(type), unresize_v->tmp_size(type));
		} else {
			tmp_width = in.width();
			tmp_height = out.height();

			tmp_size = std::max(unresize_v->tmp_size(type), unresize_h->tmp_size(type));
		}
	}

	int tmp_stride = ceil_n(tmp_width, ALIGNMENT / pxsize);
	auto tmp_frame = allocate_buffer((size_t)tmp_stride * tmp_height, type);
	auto tmp_buffer = allocate_buffer(tmp_size, type);

	convert_frame(in, src, PixelType::BYTE, type, true, false);

	measure_time(times, [&]()
	{
		for (int p = 0; p < src.planes(); ++p) {
			ImageTile src_tile{ src.data(p), src.stride() * pxsize, src.width(), src.height(), default_pixel_format(type) };
			ImageTile dst_tile{ dst.data(p), dst.stride() * pxsize, dst.width(), dst.height(), default_pixel_format(type) };
			ImageTile tmp_tile{ tmp_frame.data(), tmp_stride * pxsize, tmp_width, tmp_height, default_pixel_format(type) };

			if (skip_h && skip_v) {
				copy_image_tile(src_tile, dst_tile);
			} else if (skip_h && !skip_v) {
				unresize_v->process(src_tile, dst_tile, tmp_buffer.data());
			} else if (skip_v && !skip_h) {
				unresize_h->process(src_tile, dst_tile, tmp_buffer.data());
			} else {
				ImageTile tmp_tile{ tmp_frame.data(), tmp_stride * pxsize, tmp_width, tmp_height, default_pixel_format(type) };

				if (hfirst) {
					unresize_h->process(src_tile, tmp_tile, tmp_buffer.data());
					unresize_v->process(tmp_tile, dst_tile, tmp_buffer.data());
				} else {
					unresize_v->process(src_tile, tmp_tile, tmp_buffer.data());
					unresize_h->process(tmp_tile, dst_tile, tmp_buffer.data());
				}
			}
		}
	});

	convert_frame(dst, out, type, PixelType::BYTE, true, false);
}

} // namespace


int unresize_main(int argc, const char **argv)
{
	if (argc < 5) {
		usage();
		return -1;
	}

	AppContext c{};

	c.infile  = argv[1];
	c.outfile = argv[2];
	c.width   = std::stoi(argv[3]);
	c.height  = std::stoi(argv[4]);
	c.shift_w = 0.0;
	c.shift_h = 0.0;
	c.times   = 1;
	c.cpu     = CPUClass::CPU_NONE;
	c.pixtype = PixelType::FLOAT;

	parse_opts(argv + 5, argv + argc, std::begin(OPTIONS), std::end(OPTIONS), &c, nullptr);

	Frame in{ read_frame_bmp(c.infile) };
	Frame out{ c.width, c.height, 1, in.planes() };

	bool skip_h = in.width() == out.width();
	bool skip_v = in.height() == out.height();

	unresize::Unresize unresize_h;
	unresize::Unresize unresize_v;
	
	if (!skip_h)
		unresize_h = unresize::Unresize{ true, in.width(), out.width(), c.shift_w, c.cpu };
	if (!skip_v)
		unresize_v = unresize::Unresize{ false, in.height(), out.height(), c.shift_h, c.cpu };

	execute(&unresize_h, &unresize_v, in, out, c.times, c.pixtype);
	write_frame_bmp(out, c.outfile);

	return 0;
}

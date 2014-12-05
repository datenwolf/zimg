#include <cstddef>
#include <cstring>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include "Common/align.h"
#include "Common/cpuinfo.h"
#include "Common/pixel.h"
#include "Common/tile.h"
#include "Resize/filter.h"
#include "Resize/resize.h"
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
	std::unique_ptr<resize::Filter> filter;
	double shift_w;
	double shift_h;
	double sub_w;
	double sub_h;
	int times;
	CPUClass cpu;
	PixelType pixtype;
};

int select_filter(const char **opt, const char **lastopt, void *p, void *)
{
	AppContext *c = (AppContext *)p;

	if (lastopt - opt < 2)
		throw std::invalid_argument{ "insufficient arguments for option filter" };

	const char *filter = opt[1];

	if (!strcmp(filter, "point"))
		c->filter.reset(new resize::PointFilter{});
	else if (!strcmp(filter, "bilinear"))
		c->filter.reset(new resize::BilinearFilter{});
	else if (!strcmp(filter, "bicubic"))
		c->filter.reset(new resize::BicubicFilter{ 1.0 / 3.0, 1.0 / 3.0 });
	else if (!strcmp(filter, "lanczos"))
		c->filter.reset(new resize::LanczosFilter{ 4 });
	else if (!strcmp(filter, "spline16"))
		c->filter.reset(new resize::Spline16Filter{});
	else if (!strcmp(filter, "spline36"))
		c->filter.reset(new resize::Spline36Filter{});
	else
		throw std::invalid_argument{ "unsupported filter type" };

	return 2;
}

const AppOption OPTIONS[] = {
		{ "filter",   OptionType::OPTION_SPECIAL,   0, select_filter },
		{ "shift-w",  OptionType::OPTION_FLOAT,     offsetof(AppContext, shift_w) },
		{ "shift-h",  OptionType::OPTION_FLOAT,     offsetof(AppContext, shift_h) },
		{ "sub-w",    OptionType::OPTION_FLOAT,     offsetof(AppContext, sub_w) },
		{ "sub-h",    OptionType::OPTION_FLOAT,     offsetof(AppContext, sub_h) },
		{ "times",    OptionType::OPTION_INTEGER,   offsetof(AppContext, times) },
		{ "cpu",      OptionType::OPTION_CPUCLASS,  offsetof(AppContext, cpu) },
		{ "pixtype",  OptionType::OPTION_PIXELTYPE, offsetof(AppContext, pixtype) }
};

void usage()
{
	std::cout << "resize infile outfile w h [--filter filter] [--shift-w shift] [--shift-h shift] [--sub-w w] [--sub-h h] [--times n] [--cpu cpu] [--pixtype type]\n";
	std::cout << "    infile              input BMP file\n";
	std::cout << "    outfile             output BMP file\n";
	std::cout << "    w                   output width\n";
	std::cout << "    h                   output height\n";
	std::cout << "    --filter            resampling filter\n";
	std::cout << "    --shift-w           horizontal shift\n";
	std::cout << "    --shift-h           vertical shift\n";
	std::cout << "    --sub-w             subwindow width\n";
	std::cout << "    --sub-h             subwindow height\n";
	std::cout << "    --times             number of cycles\n";
	std::cout << "    --cpu               select CPU type\n";
	std::cout << "    --pixtype           select pixel format\n";
}

void execute(const resize::Resize *resize_h, const resize::Resize *resize_v, const Frame &in, Frame &out, int times, PixelType type)
{
	int pxsize = pixel_size(type);
	int planes = in.planes();

	Frame src{ in.width(), in.height(), pxsize, planes };
	Frame dst{ out.width(), out.height(), pxsize, planes };

	bool hfirst = resize::resize_horizontal_first((double)out.width() / in.width(), (double)out.height() / in.height());
	bool skip_h = !resize_h;
	bool skip_v = !resize_v;

	int tmp_width = 0;
	int tmp_height = 0;

	if (!skip_h && !skip_v) {
		tmp_width = hfirst ? out.width() : in.width();
		tmp_height = hfirst ? in.height() : out.height();
	}

	int tmp_stride = ceil_n(tmp_width + TILE_WIDTH, TILE_WIDTH);
	auto tmp_frame = allocate_buffer((size_t)tmp_stride * ceil_n(tmp_height, TILE_HEIGHT), type);

	convert_frame(in, src, PixelType::BYTE, type, true, false);

	measure_time(times, [&]()
	{
		PlaneDescriptor desc{ type };

		for (int p = 0; p < planes; ++p) {
			ImageTile<const void> src_tile{ src.data(p), &desc, src.stride() * pxsize };
			ImageTile<void> dst_tile{ dst.data(p), &desc, dst.stride() * pxsize };

			int top, left, bottom, right;

			if (skip_h && skip_v) {
				dst = src;
			} else if ((skip_h && !skip_v) || (skip_v && !skip_h)) {
				const resize::Resize *r = skip_v ? resize_h : resize_v;

				for (int i = 0; i < dst.height(); i += TILE_HEIGHT) {
					for (int j = 0; j < dst.width(); j += TILE_WIDTH) {
						r->dependent_rect(i, j, i + TILE_HEIGHT, j + TILE_WIDTH, &top, &left, &bottom, &right);
						r->process(src_tile.sub_tile(top, left), dst_tile.sub_tile(i, j), i, j);
					}
				}
			} else {
				ImageTile<void> tmp_tile{ tmp_frame.data(), &desc, tmp_stride * pxsize };

				const resize::Resize *r1 = hfirst ? resize_h : resize_v;
				const resize::Resize *r2 = hfirst ? resize_v : resize_h;

				for (int i = 0; i < tmp_height; i += TILE_HEIGHT) {
					for (int j = 0; j < tmp_width; j += TILE_WIDTH) {
						r1->dependent_rect(i, j, i + TILE_HEIGHT, j + TILE_WIDTH, &top, &left, &bottom, &right);
						r1->process(src_tile.sub_tile(top, left), tmp_tile.sub_tile(i, j), i, j);
					}
				}
				for (int i = 0; i < dst.height(); i += TILE_HEIGHT) {
					for (int j = 0; j < dst.width(); j += TILE_WIDTH) {
						r2->dependent_rect(i, j, i + TILE_HEIGHT, j + TILE_WIDTH, &top, &left, &bottom, &right);
						r2->process(tmp_tile.sub_tile(top, left), dst_tile.sub_tile(i, j), i, j);
					}
				}
			}
		}
	});

	convert_frame(dst, out, type, PixelType::BYTE, true, false);
}

} // namespace


int resize_main(int argc, const char **argv)
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
	c.shift_h = 0.0;
	c.shift_w = 0.0;
	c.sub_w   = -1.0;
	c.sub_h   = -1.0;
	c.times   = 1;
	c.pixtype = PixelType::FLOAT;
	c.cpu     = CPUClass::CPU_NONE;

	parse_opts(argv + 5, argv + argc, std::begin(OPTIONS), std::end(OPTIONS), &c, nullptr);

	Frame in{ read_frame_bmp(c.infile) };
	Frame out{ c.width, c.height, 1, in.planes() };

	if (c.sub_w < 0.0)
		c.sub_w = in.width();
	if (c.sub_h < 0.0)
		c.sub_h = in.height();
	if (!c.filter)
		c.filter.reset(new resize::BilinearFilter{});

	bool skip_h = in.width() == c.width && c.shift_w == 0.0 && c.sub_w == in.width();
	bool skip_v = in.height() == c.height && c.shift_h == 0.0 && c.sub_h == in.height();

	resize::Resize resize_h;
	resize::Resize resize_v;

	if (!skip_h)
		resize_h = resize::Resize{ *c.filter, true, in.width(), c.width, c.shift_w, c.sub_w, c.cpu };
	if (!skip_v)
		resize_v = resize::Resize{ *c.filter, false, in.height(), c.height, c.shift_h, c.sub_h, c.cpu };

	execute(skip_h ? nullptr : &resize_h, skip_v ? nullptr : &resize_v, in, out, c.times, c.pixtype);
	write_frame_bmp(out, c.outfile);

	return 0;
}

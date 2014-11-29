#pragma once

#ifdef ZIMG_X86

#ifndef ZIMG_DEPTH_DITHER_IMPL_X86_H_
#define ZIMG_DEPTH_DITHER_IMPL_X86_H_

#include "dither_impl.h"

namespace zimg {;

enum class CPUClass;

namespace depth {;

class DitherConvert;

class OrderedDitherX86 : public OrderedDither {
	template <int N, int M>
	struct Max {
		static const int value = N > M ? N : M;
	};

	template <int N, int M>
	struct Div {
		static const int value = N / M;
	};
protected:
	explicit OrderedDitherX86(const float *dither) : OrderedDither(dither)
	{}

	template <class T, class U, class Policy, class Unpack, class Pack, class ToFloat, class FromFloat, class ToFloatScalar, class FromFloatScalar>
	void process(const ImageTile &src, const ImageTile &dst, Policy policy, Unpack unpack, Pack pack,
	             ToFloat to_float, FromFloat from_float, ToFloatScalar to_float_scalar, FromFloatScalar from_float_scalar) const
	{
		typedef typename Policy::type vector_type;
		typedef typename Unpack::type src_vector_type;
		typedef typename Pack::type dst_vector_type;

		typedef Max<Unpack::loop_step, Pack::loop_step> loop_step;
		typedef Div<loop_step::value, Unpack::loop_step> loop_unroll_unpack;
		typedef Div<loop_step::value, Pack::loop_step> loop_unroll_pack;

		TileView<const T> src_view{ src };
		TileView<U> dst_view{ dst };

		const float *dither_data = m_dither.data();

		float scale = 1.0f / (float)(1 << (dst.format.depth - 1));
		vector_type scale_ps = policy.set1(scale);

		for (int i = 0; i < src.height; ++i) {
			const T *src_row = src_view[i];
			U * dst_row = dst_view[i];

			const float *dither_row = &dither_data[(i % NUM_DITHERS_V) * NUM_DITHERS_H];
			int m = 0;

			src_vector_type src_unpacked[loop_unroll_unpack::value * Unpack::unpacked_count];
			dst_vector_type dst_unpacked[loop_unroll_pack::value * Pack::unpacked_count];

			for (int j = 0; j < mod(src.width, loop_step::value); j += loop_step::value) {
				for (int k = 0; k < loop_unroll_unpack::value; ++k) {
					unpack.unpack(&src_unpacked[k * Unpack::unpacked_count], &src_row[j + k * Unpack::loop_step]);
				}

				for (int k = 0; k < loop_unroll_pack::value * Pack::unpacked_count; ++k) {
					vector_type x = to_float(src_unpacked[k]);
					vector_type d = policy.load(&dither_row[m]);

					d = policy.mul(d, scale_ps);
					x = policy.add(x, d);

					dst_unpacked[k] = from_float(x);

					m += Policy::vector_size;
				}

				for (int k = 0; k < loop_unroll_pack::value; ++k) {
					pack.pack(&dst_row[j + k * Pack::loop_step], &dst_unpacked[k * Pack::unpacked_count]);
				}

				m %= NUM_DITHERS_H;
			}

			for (int j = mod(src.width, loop_step::value); j < src.width; ++j) {
				float x = to_float_scalar(src_row[j]);
				float d = dither_row[m++];

				dst_row[j] = from_float_scalar(x + d * scale);
				m %= NUM_DITHERS_H;
			}
		}
	}
};

DitherConvert *create_ordered_dither_sse2(const float *dither);
DitherConvert *create_ordered_dither_avx2(const float *dither);

DitherConvert *create_ordered_dither_x86(const float *dither, CPUClass cpu);

} // namespace depth
} // namespace zimg

#endif // ZIMG_DEPTH_DITHER_IMPL_X86_H_
#endif // ZIMG_X86

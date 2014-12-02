#pragma once

#ifdef ZIMG_X86

#ifndef ZIMG_DEPTH_DEPTH_CONVERT_X86_H_
#define ZIMG_DEPTH_DEPTH_CONVERT_X86_H_

#include "Common/align.h"
#include "depth_convert.h"

namespace zimg {;

enum class CPUClass;

namespace depth {;

class DepthConvert;

/**
 * Shared DepthConvert helper for all x86 implementations.
 */
class DepthConvertX86 : public DepthConvert {
	template <int N, int M>
	struct Max {
		static const int value = N > M ? N : M;
	};

	template <int N, int M>
	struct Div {
		static const int value = N / M;
	};
protected:
	template <class T, class U, class Unpack, class Pack, class VectorOp, class ScalarOp>
	void process(const ImageTile &src, const ImageTile &dst, Unpack unpack, Pack pack, VectorOp op, ScalarOp scalar_op) const
	{
		typedef typename Unpack::type src_vector_type;
		typedef typename Pack::type dst_vector_type;

		typedef Max<Unpack::loop_step, Pack::loop_step> loop_step;
		typedef Div<loop_step::value, Unpack::loop_step> loop_unroll_unpack;
		typedef Div<loop_step::value, Pack::loop_step> loop_unroll_pack;

		TileView<const T> src_view{ src };
		TileView<U> dst_view{ dst };

		src_vector_type src_unpacked[loop_unroll_unpack::value * Unpack::unpacked_count];
		dst_vector_type dst_unpacked[loop_unroll_pack::value * Pack::unpacked_count];

		for (int i = 0; i < src.height; ++i) {
			const T *src_ptr = src_view[i];
			U *dst_ptr = dst_view[i];

			for (int j = 0; j < floor_n(src.width, loop_step::value); j += loop_step::value) {
				for (int k = 0; k < loop_unroll_unpack::value; ++k) {
					unpack.unpack(&src_unpacked[k * Unpack::unpacked_count], &src_ptr[j + k * Unpack::loop_step]);
				}

				for (int k = 0; k < loop_unroll_pack::value * Pack::unpacked_count; ++k) {
					dst_unpacked[k] = op(src_unpacked[k]);
				}

				for (int k = 0; k < loop_unroll_pack::value; ++k) {
					pack.pack(&dst_ptr[j + k * Pack::loop_step], &dst_unpacked[k * Pack::unpacked_count]);
				}
			}
			for (int j = floor_n(src.width, loop_step::value); j < src.width; ++j) {
				dst_ptr[j] = scalar_op(src_ptr[j]);
			}
		}
	}
};

DepthConvert *create_depth_convert_sse2();
DepthConvert *create_depth_convert_avx2();

DepthConvert *create_depth_convert_x86(CPUClass cpu);

} // namespace depth
} // namespace zimg

#endif // ZIMG_DEPTH_DEPTH_CONVERT_X86_H_

#endif // ZIMG_X86

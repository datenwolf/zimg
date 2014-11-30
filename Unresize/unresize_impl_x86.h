#pragma once

#ifdef ZIMG_X86

#ifndef ZIMG_UNRESIZE_UNRESIZE_IMPL_X86_H_
#define ZIMG_UNRESIZE_UNRESIZE_IMPL_X86_H_

namespace zimg {;

enum class CPUClass;

namespace unresize {;

class UnresizeImpl;
struct BilinearContext;

UnresizeImpl *create_unresize_impl_h_sse2(const BilinearContext &context);
UnresizeImpl *create_unresize_impl_h_avx2(const BilinearContext &context);

UnresizeImpl *create_unresize_impl_v_sse2(const BilinearContext &context);
UnresizeImpl *create_unresize_impl_v_avx2(const BilinearContext &context);

/**
 * Create an appropriate x86 optimized ResizeImpl for the given CPU.
 *
 * @see create_resize_impl
 */
UnresizeImpl *create_unresize_impl_x86(const BilinearContext &context, bool horizontal, CPUClass cpu);

} // namespace unresize
} // namespace zimg

#endif // ZIMG_UNRESIZE_UNRESIZE_IMPL_X86_H_

#endif // ZIMG_X86

#pragma once

#ifdef ZIMG_X86

#ifndef ZIMG_RESIZE_RESIZE_IMPL_X86_H_
#define ZIMG_RESIZE_RESIZE_IMPL_X86_H_

namespace zimg {;

enum class CPUClass;

namespace resize {;

class EvaluatedFilter;
class ResizeImpl;

ResizeImpl *create_resize_impl_h_sse2(const EvaluatedFilter &filter);

ResizeImpl *create_resize_impl_h_avx2(const EvaluatedFilter &filter);

ResizeImpl *create_resize_impl_v_sse2(const EvaluatedFilter &filter);

ResizeImpl *create_resize_impl_v_avx2(const EvaluatedFilter &filter);

/**
 * Create an appropriate x86 optimized ResizeImpl for the given CPU.
 *
 * @see create_resize_impl
 */
ResizeImpl *create_resize_impl_x86(const EvaluatedFilter &filter, bool horizontal, CPUClass cpu);

} // namespace resize
} // namespace zimg

#endif // ZIMG_RESIZE_RESIZE_IMPL_X86_H_

#endif // ZIMG_X86

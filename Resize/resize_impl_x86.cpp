#ifdef ZIMG_X86

#include "Common/cpuinfo.h"
#include "resize_impl_x86.h"

namespace zimg {;
namespace resize {;

ResizeImpl *create_resize_impl_x86(const EvaluatedFilter &filter, bool horizontal, CPUClass cpu)
{
	X86Capabilities caps = query_x86_capabilities();
	ResizeImpl *ret;

	if (horizontal) {
		if (cpu == CPUClass::CPU_X86_AUTO) {
			if (caps.avx2)
				ret = create_resize_impl_h_avx2(filter);
			else if (caps.sse2)
				ret = create_resize_impl_h_sse2(filter);
			else
				ret = nullptr;
		} else if (cpu >= CPUClass::CPU_X86_AVX2) {
			ret = create_resize_impl_h_avx2(filter);
		} else if (cpu >= CPUClass::CPU_X86_SSE2) {
			ret = create_resize_impl_h_sse2(filter);
		} else {
			ret = nullptr;
		}
	} else {
		if (cpu == CPUClass::CPU_X86_AUTO) {
			if (caps.avx2)
				ret = create_resize_impl_v_avx2(filter);
			else if (caps.sse2)
				ret = create_resize_impl_v_sse2(filter);
			else
				ret = nullptr;
		} else if (cpu >= CPUClass::CPU_X86_AVX2) {
			ret = create_resize_impl_v_avx2(filter);
		} else if (cpu >= CPUClass::CPU_X86_SSE2) {
			ret = create_resize_impl_v_sse2(filter);
		} else {
			ret = nullptr;
		}
	}

	return ret;
}

} // namespace resize
} // namespace zimg

#endif // ZIMG_X86

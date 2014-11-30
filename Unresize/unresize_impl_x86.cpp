#ifdef ZIMG_X86

#include "Common/cpuinfo.h"
#include "unresize_impl_x86.h"

namespace zimg {;
namespace unresize {;

UnresizeImpl *create_unresize_impl_x86(const BilinearContext &context, bool horizontal, CPUClass cpu)
{
	X86Capabilities caps = query_x86_capabilities();
	UnresizeImpl *ret;

	if (horizontal) {
		if (cpu == CPUClass::CPU_X86_AUTO) {
			if (caps.avx2)
				ret = create_unresize_impl_h_avx2(context);
			else if (caps.sse2)
				ret = create_unresize_impl_h_sse2(context);
			else
				ret = nullptr;
		} else if (cpu >= CPUClass::CPU_X86_AVX2) {
			ret = create_unresize_impl_h_avx2(context);
		} else if (cpu >= CPUClass::CPU_X86_SSE2) {
			ret = create_unresize_impl_h_sse2(context);
		} else {
			ret = nullptr;
		}
	} else {
		if (cpu == CPUClass::CPU_X86_AUTO) {
			if (caps.avx2)
				ret = create_unresize_impl_v_avx2(context);
			else if (caps.sse2)
				ret = create_unresize_impl_v_sse2(context);
			else
				ret = nullptr;
		} else if (cpu >= CPUClass::CPU_X86_AVX2) {
			ret = create_unresize_impl_v_avx2(context);
		} else if (cpu >= CPUClass::CPU_X86_SSE2) {
			ret = create_unresize_impl_v_sse2(context);
		} else {
			ret = nullptr;
		}
	}

	return ret;
}

} // namespace unresize
} // namespace zimg

#endif // ZIMG_X86

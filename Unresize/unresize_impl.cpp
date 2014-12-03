#include "Common/cpuinfo.h"
#include "Common/except.h"
#include "Common/osdep.h"
#include "Common/tile.h"
#include "bilinear.h"
#include "unresize_impl.h"
#include "unresize_impl_x86.h"

namespace zimg {;
namespace unresize {;

namespace {;

class UnresizeImplH_C : public UnresizeImpl {
public:
	UnresizeImplH_C(const BilinearContext &context) : UnresizeImpl(context)
	{}

	void process_f16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, void *tmp) const override
	{
		throw ZimgUnsupportedError{ "f16 not supported in C impl" };
	}

	void process_f32(const ImageTile<const float> &src, const ImageTile<float> &dst, void *tmp) const override
	{
		for (int i = 0; i < dst.descriptor()->height; ++i) {
			filter_scanline_h_forward(m_context, src, (float *)tmp, i, 0, dst.descriptor()->width, ScalarPolicy_F32{});
			filter_scanline_h_back(m_context, (const float *)tmp, dst, i, dst.descriptor()->width, 0, ScalarPolicy_F32{});
		}
	}
};

class UnresizeImplV_C : public UnresizeImpl {
public:
	UnresizeImplV_C(const BilinearContext &context) : UnresizeImpl(context)
	{}

	void process_f16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, void *tmp) const override
	{
		throw ZimgUnsupportedError{ "f16 not supported in C impl" };
	}

	void process_f32(const ImageTile<const float> &src, const ImageTile<float> &dst, void *tmp) const override
	{
		for (int i = 0; i < dst.descriptor()->height; ++i) {
			filter_scanline_v_forward(m_context, src, dst, i, 0, dst.descriptor()->width, ScalarPolicy_F32{});
		}
		for (int i = dst.descriptor()->height; i > 0; --i) {
			filter_scanline_v_back(m_context, dst, i, 0, dst.descriptor()->width, ScalarPolicy_F32{});
		}
	}
};

} // namespace


UnresizeImpl::UnresizeImpl(const BilinearContext &context) :
	m_context(context)
{
}

UnresizeImpl::~UnresizeImpl()
{
}

UnresizeImpl *create_unresize_impl(bool horizontal, int src_dim, int dst_dim, double shift, CPUClass cpu)
{
	BilinearContext context;
	BilinearContext vcontext;
	UnresizeImpl *ret = nullptr;

	if (dst_dim == src_dim)
		throw ZimgIllegalArgument("input dimensions must differ from output");
	if (dst_dim > src_dim)
		throw ZimgIllegalArgument("input dimension must be greater than output");

	context = create_bilinear_context(dst_dim, src_dim, shift);
#ifdef ZIMG_X86
	ret = create_unresize_impl_x86(context, horizontal, cpu);
#endif
	if (!ret)
		ret = horizontal ? (UnresizeImpl *)new UnresizeImplH_C{ context } : (UnresizeImpl *)new UnresizeImplV_C(context);

	return ret;
}

} // namespace unresize
} // namespace zimg

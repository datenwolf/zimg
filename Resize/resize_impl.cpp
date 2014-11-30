#include "Common/cpuinfo.h"
#include "Common/except.h"
#include "Common/tile.h"
#include "resize_impl.h"
#include "resize_impl_x86.h"

namespace zimg {;
namespace resize {;

namespace {;

class ResizeImplH_C final : public ResizeImpl {
public:
	ResizeImplH_C(const EvaluatedFilter &filter) : ResizeImpl(filter)
	{}

	void process_u16(const ImageTile &src, const ImageTile &dst, void *) const override
	{
		const EvaluatedFilter &filter = m_filter;
		filter_plane_h_scalar(filter, src, dst, 0, src.height, 0, filter.height(), ScalarPolicy_U16{});
	}

	void process_f16(const ImageTile &, const ImageTile &, void *) const override
	{
		throw ZimgUnsupportedError{ "f16 not supported in C impl" };
	}

	void process_f32(const ImageTile &src, const ImageTile &dst, void *) const override
	{
		const EvaluatedFilter &filter = m_filter;
		filter_plane_h_scalar(filter, src, dst, 0, src.height, 0, filter.height(), ScalarPolicy_F32{});
	}
};

class ResizeImplV_C final : public ResizeImpl {
public:
	ResizeImplV_C(const EvaluatedFilter &filter) : ResizeImpl(filter)
	{}

	void process_u16(const ImageTile &src, const ImageTile &dst, void *) const override
	{
		const EvaluatedFilter &filter = m_filter;
		filter_plane_v_scalar(filter, src, dst, 0, filter.height(), 0, src.width, ScalarPolicy_U16{});
	}

	void process_f16(const ImageTile &, const ImageTile &, void *) const override
	{
		throw ZimgUnsupportedError{ "f16 not supported in C impl" };
	}

	void process_f32(const ImageTile &src, const ImageTile &dst, void *) const override
	{
		const EvaluatedFilter &filter = m_filter;
		filter_plane_v_scalar(filter, src, dst, 0, filter.height(), 0, src.width, ScalarPolicy_F32{});
	}
};

} // namespace


ResizeImpl::ResizeImpl(const EvaluatedFilter &filter) : m_filter{ filter } 
{
}

ResizeImpl::~ResizeImpl()
{
}

ResizeImpl *create_resize_impl(const Filter &f, bool horizontal, int src_dim, int dst_dim, double shift, double width, CPUClass cpu)
{
	EvaluatedFilter filter = compute_filter(f, src_dim, dst_dim, shift, width);
	ResizeImpl *ret = nullptr;

#ifdef ZIMG_X86
	ret = create_resize_impl_x86(filter, horizontal, cpu);
#endif
	if (!ret)
		ret = horizontal ? (ResizeImpl *)new ResizeImplH_C(filter) : (ResizeImpl *)new ResizeImplV_C(filter);

	return ret;
}

} // namespace resize
} // namespace zimg

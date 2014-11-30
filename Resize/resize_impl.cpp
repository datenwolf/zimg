#include <algorithm>
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
	ResizeImplH_C(const EvaluatedFilter &filter) : ResizeImpl(filter, true)
	{}

	void process_u16(const ImageTile &src, const ImageTile &dst, int i, int j, void *tmp) const override
	{
		const EvaluatedFilter &filter = m_filter;
		resize_tile_h_scalar(filter, src, dst, j, 0, 0, dst.height, dst.width, ScalarPolicy_U16{});
	}

	void process_f16(const ImageTile &src, const ImageTile &dst, int i, int j, void *tmp) const override
	{
		throw ZimgUnsupportedError{ "f16 not supported in C impl" };
	}

	void process_f32(const ImageTile &src, const ImageTile &dst, int i, int j, void *tmp) const override
	{
		const EvaluatedFilter &filter = m_filter;
		resize_tile_h_scalar(filter, src, dst, j, 0, 0, dst.height, dst.width, ScalarPolicy_F32{});
	}
};

class ResizeImplV_C final : public ResizeImpl {
public:
	ResizeImplV_C(const EvaluatedFilter &filter) : ResizeImpl(filter, false)
	{}

	void process_u16(const ImageTile &src, const ImageTile &dst, int i, int j, void *tmp) const override
	{
		const EvaluatedFilter &filter = m_filter;
		resize_tile_v_scalar(filter, src, dst, i, 0, 0, dst.height, dst.width, ScalarPolicy_U16{});
	}

	void process_f16(const ImageTile &src, const ImageTile &dst, int i, int j, void *tmp) const override
	{
		throw ZimgUnsupportedError{ "f16 not supported in C impl" };
	}

	void process_f32(const ImageTile &src, const ImageTile &dst, int i, int j, void *tmp) const override
	{
		const EvaluatedFilter &filter = m_filter;
		resize_tile_v_scalar(filter, src, dst, i, 0, 0, dst.height, dst.width, ScalarPolicy_F32{});
	}
};

} // namespace


ResizeImpl::ResizeImpl(const EvaluatedFilter &filter, bool horizontal) : m_filter{ filter }, m_horizontal{ horizontal }
{
}

ResizeImpl::~ResizeImpl()
{
}

void ResizeImpl::dependent_rect(int dst_top, int dst_left, int dst_bottom, int dst_right, int *src_top, int *src_left, int *src_bottom, int *src_right) const
{
	if (m_horizontal) {
		int left = m_filter.left()[dst_left];
		int right = m_filter.left()[dst_right - 1] + m_filter.width();
		
		*src_top = dst_top;
		*src_left = left;
		*src_bottom = dst_bottom;
		*src_right = right;
	} else {
		int top = m_filter.left()[dst_top];
		int bottom = m_filter.left()[dst_bottom - 1] + m_filter.width();

		*src_top = top;
		*src_left = dst_left;
		*src_bottom = bottom;
		*src_right = dst_right;
	}
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

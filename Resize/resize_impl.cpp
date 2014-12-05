#include <algorithm>
#include "Common/cpuinfo.h"
#include "Common/except.h"
#include "Common/tile.h"
#include "resize_impl.h"
#include "resize_impl_x86.h"

namespace zimg {;
namespace resize {;

namespace {;

struct ScalarPolicy_U16 {
	typedef uint16_t data_type;
	typedef int32_t num_type;

	int32_t coeff(const EvaluatedFilter &filter, int row, int k)
	{
		return filter.data_i16()[row * filter.stride_i16() + k];
	}

	int32_t load(const uint16_t *src)
	{
		uint16_t x = *src;
		return (int32_t)x + (int32_t)INT16_MIN; // Make signed.
	}

	void store(uint16_t *dst, int32_t x)
	{
		// Convert from 16.14 to 16.0.
		x = ((x + (1 << 13)) >> 14) - (int32_t)INT16_MIN;

		// Clamp out of range values.
		x = std::max(std::min(x, (int32_t)UINT16_MAX), (int32_t)0);

		*dst = (uint16_t)x;
	}
};

struct ScalarPolicy_F32 {
	typedef float data_type;
	typedef float num_type;

	float coeff(const EvaluatedFilter &filter, int row, int k)
	{
		return filter.data()[row * filter.stride() + k];
	}

	float load(const float *src) { return *src; }

	void store(float *dst, float x) { *dst = x; }
};

template <class T, class Policy>
void resize_tile_h_scalar(const EvaluatedFilter &filter, const ImageTile<const T> &src, const ImageTile<T> &dst, int n, Policy policy)
{
	typedef typename Policy::data_type data_type;
	typedef typename Policy::num_type num_type;

	int left_base = filter.left()[n];
	
	for (int i = 0; i < TILE_HEIGHT; ++i) {
		for (int j = 0; j < TILE_WIDTH; ++j) {
			int filter_row = n + j;
			int left = filter.left()[filter_row] - left_base;

			num_type accum = 0;

			for (int k = 0; k < filter.width(); ++k) {
				num_type coeff = policy.coeff(filter, filter_row, k);
				num_type x = policy.load(&src[i][left + k]);

				accum += coeff * x;
			}

			policy.store(&dst[i][j], accum);
		}
	}
}

template <class T, class Policy>
void resize_tile_v_scalar(const EvaluatedFilter &filter, const ImageTile<const T> &src, const ImageTile<T> &dst, int n, Policy policy)
{
	typedef typename Policy::data_type data_type;
	typedef typename Policy::num_type num_type;

	int top_base = filter.left()[n];

	for (int i = 0; i < TILE_HEIGHT; ++i) {
		int filter_row = n + i;
		int top = filter.left()[filter_row] - top_base;

		for (int j = 0; j < TILE_WIDTH; ++j) {
			num_type accum = 0;

			for (int k = 0; k < filter.width(); ++k) {
				num_type coeff = policy.coeff(filter, filter_row, k);
				num_type x = policy.load(&src[top + k][j]);

				accum += coeff * x;
			}

			policy.store(&dst[i][j], accum);
		}
	}
}


class ResizeImplH_C final : public ResizeImpl {
public:
	ResizeImplH_C(const EvaluatedFilter &filter) : ResizeImpl(filter, true)
	{}

	void process_u16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, int i, int j) const override
	{
		const EvaluatedFilter &filter = m_filter;
		resize_tile_h_scalar(filter, src, dst, j, ScalarPolicy_U16{});
	}

	void process_f16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, int i, int j) const override
	{
		throw ZimgUnsupportedError{ "f16 not supported in C impl" };
	}

	void process_f32(const ImageTile<const float> &src, const ImageTile<float> &dst, int i, int j) const override
	{
		const EvaluatedFilter &filter = m_filter;
		resize_tile_h_scalar(filter, src, dst, j, ScalarPolicy_F32{});
	}
};

class ResizeImplV_C final : public ResizeImpl {
public:
	ResizeImplV_C(const EvaluatedFilter &filter) : ResizeImpl(filter, false)
	{}

	void process_u16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, int i, int j) const override
	{
		const EvaluatedFilter &filter = m_filter;
		resize_tile_v_scalar(filter, src, dst, i, ScalarPolicy_U16{});
	}

	void process_f16(const ImageTile<const uint16_t> &src, const ImageTile<uint16_t> &dst, int i, int j) const override
	{
		throw ZimgUnsupportedError{ "f16 not supported in C impl" };
	}

	void process_f32(const ImageTile<const float> &src, const ImageTile<float> &dst, int i, int j) const override
	{
		const EvaluatedFilter &filter = m_filter;
		resize_tile_v_scalar(filter, src, dst, i, ScalarPolicy_F32{});
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

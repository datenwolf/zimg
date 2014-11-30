#pragma once

#ifndef ZIMG_UNRESIZE_UNRESIZE_H_
#define ZIMG_UNRESIZE_UNRESIZE_H_

#include <cstdint>
#include <memory>

namespace zimg {;

enum class CPUClass;
enum class PixelType;

struct ImageTile;

namespace unresize {;

class UnresizeImpl;

/**
 * Unresize: reverses the effect of the bilinear scaling method.
 *
 * Linear interpolation in one dimension from an input dimension N to an
 * output dimension M can be represented as the matrix product:
 *
 * A x = y
 *
 * A is the interpolation function
 * x is the original vector
 * y is the resized vector
 *
 *
 * Unresize attempts to recover x given the resized vector y.
 * This is done by the method of least squares.
 *
 * A' A x = A' y
 *
 * A' is the transpose of A
 *
 *
 * The problem resolves to solving a linear system.
 *
 * P x = y'
 *
 * P is (A' A)
 * y' is (A' y)
 *
 *
 * Given the width of the bilinear filter, P is a tridiagonal matrix of
 * dimension N, and so the system can be solved by simple substitution after
 * LU factorization.
 *
 * Using a convention that U has a main diagonal of ones, the factoization is
 * given by the following.
 *
 *
 * The following names will be given to relevant diagonals.
 *
 * a(i) = P(i, i)
 * b(i) = P(i, i + 1)
 * c(i) = P(i, i - 1)
 * l(i) = L(i, i)
 * u(i) = U(i, i + 1)
 *
 * The computation of l and u can be described by the following procedure.
 *
 * l(1) = a(1)
 * u(1) = b(1) / a(1)
 *
 * FOR (i = 1 : N - 1)
 *   l(i) = a(i) - c(i) * u(i - 1)
 *   u(i) = b(i) / l(i)
 *
 * l(N) = a(N) - c(N) * u(N - 1)
 *
 *
 * The solution to the system can be described by the procedure.
 *
 * L U x = y'
 *
 * z(1) = y'(1) / l(1)
 * FOR (i = 2 : N)
 *   z(i) = (y'(i) - c(i) * z(i - 1)) / l(i)
 *
 * x(N) = z(N)
 * FOR (i = N - 1 : 1)
 *   x(i) = z(i) - u(i) * x'(i + 1)
 *
 *
 * The implementation of Unresize caches the values of P, l, u, and c for given
 * dimensions N and M. Execution is done by first computing y' and then
 * performing the tridiagonal algorithm to obtain x.
 *
 * Generalization to two dimensions is done by processing each dimension.
 *
 *
 * In the class comments below, "input" refers to the upsampled image
 * and "output" refers to the unresized image.
 */
class Unresize {
	std::shared_ptr<UnresizeImpl> m_impl;
	int m_dst_dim;
	bool m_horizontal;
public:
	/**
	 * Initialize a null context. Cannot be used for execution.
	 */
	Unresize() = default;

	/**
	 * Initialize a context to unresize a given bilinear resampling.
	 *
	 * @param horizontal whether resizing is to be done horizontally or vertically
	 * @param src_dim input dimension
	 * @param dst_dim output dimension
	 * @param shift center shift in units of source pixels
	 * @param cpu create kernel optimized for given cpu
	 * @throws ZimgIllegalArgument on invalid dimensions
	 * @throws ZimgOutOfMemory if out of memory
	 */
	Unresize(bool horizontal, int src_dim, int dst_dim, double shift, CPUClass cpu);

	/**
	 * Destroy context.
	 */
	~Unresize();

	/**
	 * Get the size of the temporary buffer required by the filter.
	 *
	 * @param type pixel type to process
	 * @return the size of temporary buffer in units of pixels
	 */
	size_t tmp_size(PixelType type) const;

	/**
	 * Process an image. The input and output pixel formats must match.
	 * The tile must span an entire plane.
	 *
	 * @param src input tile
	 * @param dst output tile
	 * @param tmp temporary buffer (@see Unresize::tmp_size)
	 * @throws ZimgUnsupportedError if pixel type not supported
	 */
	void process(const ImageTile &src, const ImageTile &dst, void *tmp) const;
};

/**
 * Check if unresizing horizontally or vertically first is more efficient.
 *
 * @param xscale horizontal unresizing ratio
 * @param yscale vertical unresizing ratio
 * @return true if unresizing horizontally first is more efficient
 */
bool unresize_horizontal_first(double xscale, double yscale);

} // namespace unresize
} // namespace zimg

#endif // ZIMG_UNRESIZE_UNRESIZE_H_

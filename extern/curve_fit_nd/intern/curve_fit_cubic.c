/*
 * Copyright (c) 2016, DWANGO Co., Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file curve_fit_cubic.c
 *  \ingroup curve_fit
 */

#include <math.h>
#include <float.h>
#include <stdbool.h>
#include <assert.h>

#include <string.h>
#include <stdlib.h>

#include "../curve_fit_nd.h"

/* avoid re-calculating lengths multiple times */
#define USE_LENGTH_CACHE

/* store the indices in the cubic data so we can return the original indices,
 * useful when the caller has data assosiated with the curve. */
#define USE_ORIG_INDEX_DATA

typedef unsigned int uint;

#include "curve_fit_inline.h"

#ifdef _MSC_VER
#  define alloca(size) _alloca(size)
#endif

#if !defined(_MSC_VER)
#  define USE_VLA
#endif

#ifdef USE_VLA
#  ifdef __GNUC__
#    pragma GCC diagnostic ignored "-Wvla"
#  endif
#else
#  ifdef __GNUC__
#    pragma GCC diagnostic error "-Wvla"
#  endif
#endif

#define SWAP(type, a, b)  {    \
	type sw_ap;                \
	sw_ap = (a);               \
	(a) = (b);                 \
	(b) = sw_ap;               \
} (void)0


/* -------------------------------------------------------------------- */

/** \name Cubic Type & Functions
 * \{ */

typedef struct Cubic {
	/* single linked lists */
	struct Cubic *next;
#ifdef USE_ORIG_INDEX_DATA
	uint orig_span;
#endif
	/* 0: point_0, 1: handle_0, 2: handle_1, 3: point_1,
	 * each one is offset by 'dims' */
	double pt_data[0];
} Cubic;

#define CUBIC_PT(cubic, index, dims) \
	(&(cubic)->pt_data[(index) * (dims)])

#define CUBIC_VARS(c, dims, _p0, _p1, _p2, _p3) \
	double \
	*_p0 = (c)->pt_data, \
	*_p1 = _p0 + (dims), \
	*_p2 = _p1 + (dims), \
	*_p3 = _p2 + (dims); ((void)0)
#define CUBIC_VARS_CONST(c, dims, _p0, _p1, _p2, _p3) \
	const double \
	*_p0 = (c)->pt_data, \
	*_p1 = _p0 + (dims), \
	*_p2 = _p1 + (dims), \
	*_p3 = _p2 + (dims); ((void)0)


static Cubic *cubic_alloc(const uint dims)
{
	return malloc(sizeof(Cubic) + (sizeof(double) * 4 * dims));
}

static void cubic_init(
        Cubic *cubic,
        const double p0[], const double p1[], const double p2[], const double p3[],
        const uint dims)
{
	copy_vnvn(CUBIC_PT(cubic, 0, dims), p0, dims);
	copy_vnvn(CUBIC_PT(cubic, 1, dims), p1, dims);
	copy_vnvn(CUBIC_PT(cubic, 2, dims), p2, dims);
	copy_vnvn(CUBIC_PT(cubic, 3, dims), p3, dims);
}

static void cubic_free(Cubic *cubic)
{
	free(cubic);
}

/** \} */


/* -------------------------------------------------------------------- */

/** \name CubicList Type & Functions
 * \{ */

typedef struct CubicList {
	struct Cubic *items;
	uint          len;
	uint          dims;
} CubicList;

static void cubic_list_prepend(CubicList *clist, Cubic *cubic)
{
	cubic->next = clist->items;
	clist->items = cubic;
	clist->len++;
}

static double *cubic_list_as_array(
        const CubicList *clist
#ifdef USE_ORIG_INDEX_DATA
        ,
        const uint index_last,
        uint *r_orig_index
#endif
        )
{
	const uint dims = clist->dims;
	const uint array_flat_len = (clist->len + 1) * 3 * dims;

	double *array = malloc(sizeof(double) * array_flat_len);
	const double *handle_prev = &((Cubic *)clist->items)->pt_data[dims];

#ifdef USE_ORIG_INDEX_DATA
	uint orig_index_value = index_last;
	uint orig_index_index = clist->len;
	bool use_orig_index = (r_orig_index != NULL);
#endif

	/* fill the array backwards */
	const size_t array_chunk = 3 * dims;
	double *array_iter = array + array_flat_len;
	for (Cubic *citer = clist->items; citer; citer = citer->next) {
		array_iter -= array_chunk;
		memcpy(array_iter, &citer->pt_data[2 * dims], sizeof(double) * 2 * dims);
		memcpy(&array_iter[2 * dims], &handle_prev[dims], sizeof(double) * dims);
		handle_prev = citer->pt_data;

#ifdef USE_ORIG_INDEX_DATA
		if (use_orig_index) {
			r_orig_index[orig_index_index--] = orig_index_value;
			orig_index_value -= citer->orig_span;
		}
#endif
	}

#ifdef USE_ORIG_INDEX_DATA
	if (use_orig_index) {
		assert(orig_index_index == 0);
		assert(orig_index_value == 0 || index_last == 0);
		r_orig_index[orig_index_index] = index_last ? orig_index_value : 0;

	}
#endif

	/* flip tangent for first and last (we could leave at zero, but set to something useful) */

	/* first */
	array_iter -= array_chunk;
	memcpy(&array_iter[dims], handle_prev, sizeof(double) * 2 * dims);
	flip_vn_vnvn(&array_iter[0 * dims], &array_iter[1 * dims], &array_iter[2 * dims], dims);
	assert(array == array_iter);

	/* last */
	array_iter += array_flat_len - (3 * dims);
	flip_vn_vnvn(&array_iter[2 * dims], &array_iter[1 * dims], &array_iter[0 * dims], dims);

	return array;
}

static void cubic_list_clear(CubicList *clist)
{
	Cubic *cubic_next;
	for (Cubic *citer = clist->items; citer; citer = cubic_next) {
		cubic_next = citer->next;
		cubic_free(citer);
	}
	clist->items = NULL;
	clist->len  = 0;
}

/** \} */


/* -------------------------------------------------------------------- */

/** \name Cubic Evaluation
 * \{ */

static void cubic_evaluate(
        const Cubic *cubic, const double t, const uint dims,
        double r_v[])
{
	CUBIC_VARS_CONST(cubic, dims, p0, p1, p2, p3);
	const double s = 1.0 - t;

	for (uint j = 0; j < dims; j++) {
		const double p01 = (p0[j] * s) + (p1[j] * t);
		const double p12 = (p1[j] * s) + (p2[j] * t);
		const double p23 = (p2[j] * s) + (p3[j] * t);
		r_v[j] = ((((p01 * s) + (p12 * t))) * s) +
		         ((((p12 * s) + (p23 * t))) * t);
	}
}

static void cubic_calc_point(
        const Cubic *cubic, const double t, const uint dims,
        double r_v[])
{
	CUBIC_VARS_CONST(cubic, dims, p0, p1, p2, p3);
	const double s = 1.0 - t;
	for (uint j = 0; j < dims; j++) {
		r_v[j] = p0[j] * s * s * s +
		         3.0 * t * s * (s * p1[j] + t * p2[j]) + t * t * t * p3[j];
	}
}

static void cubic_calc_speed(
        const Cubic *cubic, const double t, const uint dims,
        double r_v[])
{
	CUBIC_VARS_CONST(cubic, dims, p0, p1, p2, p3);
	const double s = 1.0 - t;
	for (uint j = 0; j < dims; j++) {
		r_v[j] =  3.0 * ((p1[j] - p0[j]) * s * s + 2.0 *
		                 (p2[j] - p0[j]) * s * t +
		                 (p3[j] - p2[j]) * t * t);
	}
}

static void cubic_calc_acceleration(
        const Cubic *cubic, const double t, const uint dims,
        double r_v[])
{
	CUBIC_VARS_CONST(cubic, dims, p0, p1, p2, p3);
    const double s = 1.0 - t;
	for (uint j = 0; j < dims; j++) {
		r_v[j] = 6.0 * ((p2[j] - 2.0 * p1[j] + p0[j]) * s +
		                (p3[j] - 2.0 * p2[j] + p1[j]) * t);
	}
}

/**
 * Returns a 'measure' of the maximal discrepancy of the points specified
 * by points_offset from the corresponding cubic(u[]) points.
 */
static void cubic_calc_error(
        const Cubic *cubic,
        const double *points_offset,
        const uint points_offset_len,
        const double *u,
        const uint dims,

        double *r_error_sq_max,
        uint *r_error_index)
{
	double error_sq_max = 0.0;
	uint   error_index = 0;

	const double *pt_real = points_offset + dims;
#ifdef USE_VLA
	double        pt_eval[dims];
#else
	double       *pt_eval = alloca(sizeof(double) * dims);
#endif

	for (uint i = 1; i < points_offset_len - 1; i++, pt_real += dims) {
		cubic_evaluate(cubic, u[i], dims, pt_eval);

		const double err_sq = len_squared_vnvn(pt_real, pt_eval, dims);
		if (err_sq >= error_sq_max) {
			error_sq_max = err_sq;
			error_index = i;
		}
	}

	*r_error_sq_max   = error_sq_max;
	*r_error_index = error_index;
}

/**
 * Bezier multipliers
 */

static double B1(double u)
{
	double tmp = 1.0 - u;
	return 3.0 * u * tmp * tmp;
}

static double B2(double u)
{
	return 3.0 * u * u * (1.0 - u);
}

static double B0plusB1(double u)
{
    double tmp = 1.0 - u;
    return tmp * tmp * (1.0 + 2.0 * u);
}

static double B2plusB3(double u)
{
    return u * u * (3.0 - 2.0 * u);
}

static void points_calc_center_weighted(
        const double *points_offset,
        const uint    points_offset_len,
        const uint    dims,

        double r_center[])
{
	/*
	 * Calculate a center that compensates for point spacing.
	 */

	const double *pt_prev = &points_offset[(points_offset_len - 2) * dims];
	const double *pt_curr = pt_prev + dims;
	const double *pt_next = points_offset;

	double w_prev = len_vnvn(pt_prev, pt_curr, dims);

	zero_vn(r_center, dims);
	double w_tot = 0.0;

	for (uint i_next = 0; i_next < points_offset_len; i_next++) {
		const double w_next = len_vnvn(pt_curr, pt_next, dims);
		const double w = w_prev + w_next;
		w_tot += w;

		miadd_vn_vn_fl(r_center, pt_curr, w, dims);

		w_prev = w_next;

		pt_prev = pt_curr;
		pt_curr = pt_next;
		pt_next += dims;
	}

	if (w_tot != 0.0) {
		imul_vn_fl(r_center, 1.0 / w_tot, dims);
	}
}

/**
 * Use least-squares method to find Bezier control points for region.
 */
static void cubic_from_points(
        const double *points_offset,
        const uint    points_offset_len,
        const double *u_prime,
        const double  tan_l[],
        const double  tan_r[],
        const uint dims,

        Cubic *r_cubic)
{

	const double *p0 = &points_offset[0];
	const double *p3 = &points_offset[(points_offset_len - 1) * dims];

	/* Point Pairs */
	double alpha_l, alpha_r;
#ifdef USE_VLA
	double a[2][dims];
	double tmp[dims];
#else
	double *a[2] = {
	    alloca(sizeof(double) * dims),
	    alloca(sizeof(double) * dims),
	};
	double *tmp = alloca(sizeof(double) * dims);
#endif

	{
		double x[2] = {0.0}, c[2][2] = {{0.0}};
		const double *pt = points_offset;

		for (uint i = 0; i < points_offset_len; i++, pt += dims) {
			mul_vnvn_fl(a[0], tan_l, B1(u_prime[i]), dims);
			mul_vnvn_fl(a[1], tan_r, B2(u_prime[i]), dims);

			c[0][0] += dot_vnvn(a[0], a[0], dims);
			c[0][1] += dot_vnvn(a[0], a[1], dims);
			c[1][1] += dot_vnvn(a[1], a[1], dims);

			c[1][0] = c[0][1];

			{
				const double b0_plus_b1 = B0plusB1(u_prime[i]);
				const double b2_plus_b3 = B2plusB3(u_prime[i]);
				for (uint j = 0; j < dims; j++) {
					tmp[j] = (pt[j] - (p0[j] * b0_plus_b1)) + (p3[j] * b2_plus_b3);
				}

				x[0] += dot_vnvn(a[0], tmp, dims);
				x[1] += dot_vnvn(a[1], tmp, dims);
			}
		}

		double det_C0_C1 = c[0][0] * c[1][1] - c[0][1] * c[1][0];
		double det_C_0X  = x[1]    * c[0][0] - x[0]    * c[0][1];
		double det_X_C1  = x[0]    * c[1][1] - x[1]    * c[0][1];

		if (is_almost_zero(det_C0_C1)) {
			det_C0_C1 = c[0][0] * c[1][1] * 10e-12;
		}

		/* may still divide-by-zero, check below will catch nan values */
		alpha_l = det_X_C1 / det_C0_C1;
		alpha_r = det_C_0X / det_C0_C1;
	}

	/*
	 * The problem that the stupid values for alpha dare not put
	 * only when we realize that the sign and wrong,
	 * but even if the values are too high.
	 * But how do you evaluate it?
	 *
	 * Meanwhile, we should ensure that these values are sometimes
	 * so only problems absurd of approximation and not for bugs in the code.
	 */

	/* flip check to catch nan values */
	if (!(alpha_l >= 0.0) ||
	    !(alpha_r >= 0.0))
	{
		alpha_l = alpha_r = len_vnvn(p0, p3, dims) / 3.0;
	}

	double *p1 = CUBIC_PT(r_cubic, 1, dims);
	double *p2 = CUBIC_PT(r_cubic, 2, dims);

	copy_vnvn(CUBIC_PT(r_cubic, 0, dims), p0, dims);
	copy_vnvn(CUBIC_PT(r_cubic, 3, dims), p3, dims);

#ifdef USE_ORIG_INDEX_DATA
	r_cubic->orig_span = (points_offset_len - 1);
#endif

	/* p1 = p0 - (tan_l * alpha_l);
	 * p2 = p3 + (tan_r * alpha_r);
	 */
	msub_vn_vnvn_fl(p1, p0, tan_l, alpha_l, dims);
	madd_vn_vnvn_fl(p2, p3, tan_r, alpha_r, dims);

	/* ------------------------------------
	 * Clamping (we could make it optional)
	 */
#ifdef USE_VLA
	double center[dims];
#else
	double *center = alloca(sizeof(double) * dims);
#endif
	points_calc_center_weighted(points_offset, points_offset_len, dims, center);

	const double clamp_scale = 3.0;  /* clamp to 3x */
	double dist_sq_max = 0.0;

	{
		const double *pt = points_offset;
		for (uint i = 0; i < points_offset_len; i++, pt += dims) {
#if 0
			double dist_sq_test = sq(len_vnvn(center, pt, dims) * clamp_scale);
#else
			/* do inline */
			double dist_sq_test = 0.0;
			for (uint j = 0; j < dims; j++) {
				dist_sq_test += sq((pt[j] - center[j]) * clamp_scale);
			}
#endif
			dist_sq_max = max(dist_sq_max, dist_sq_test);
		}
	}

	double p1_dist_sq = len_squared_vnvn(center, p1, dims);
	double p2_dist_sq = len_squared_vnvn(center, p2, dims);

	if (p1_dist_sq > dist_sq_max ||
	    p2_dist_sq > dist_sq_max)
	{

		alpha_l = alpha_r = len_vnvn(p0, p3, dims) / 3.0;

		/*
		 * p1 = p0 - (tan_l * alpha_l);
		 * p2 = p3 + (tan_r * alpha_r);
		 */
		for (uint j = 0; j < dims; j++) {
			p1[j] = p0[j] - (tan_l[j] * alpha_l);
			p2[j] = p3[j] + (tan_r[j] * alpha_r);
		}

		p1_dist_sq = len_squared_vnvn(center, p1, dims);
		p2_dist_sq = len_squared_vnvn(center, p2, dims);
	}

	/* clamp within the 3x radius */
	if (p1_dist_sq > dist_sq_max) {
		isub_vnvn(p1, center, dims);
		imul_vn_fl(p1, sqrt(dist_sq_max) / sqrt(p1_dist_sq), dims);
		iadd_vnvn(p1, center, dims);
	}
	if (p2_dist_sq > dist_sq_max) {
		isub_vnvn(p2, center, dims);
		imul_vn_fl(p2, sqrt(dist_sq_max) / sqrt(p2_dist_sq), dims);
		iadd_vnvn(p2, center, dims);
	}
	/* end clamping */
}

#ifdef USE_LENGTH_CACHE
static void points_calc_coord_length_cache(
        const double *points_offset,
        const uint    points_offset_len,
        const uint    dims,

        double     *r_points_length_cache)
{
	const double *pt_prev = points_offset;
	const double *pt = pt_prev + dims;
	r_points_length_cache[0] = 0.0;
	for (uint i = 1; i < points_offset_len; i++) {
		r_points_length_cache[i] = len_vnvn(pt, pt_prev, dims);
		pt_prev = pt;
		pt += dims;
	}
}
#endif  /* USE_LENGTH_CACHE */


static void points_calc_coord_length(
        const double *points_offset,
        const uint    points_offset_len,
        const uint    dims,
#ifdef USE_LENGTH_CACHE
        const double *points_length_cache,
#endif
        double *r_u)
{
	const double *pt_prev = points_offset;
	const double *pt = pt_prev + dims;
	r_u[0] = 0.0;
	for (uint i = 1, i_prev = 0; i < points_offset_len; i++) {
		double length;

#ifdef USE_LENGTH_CACHE
		length = points_length_cache[i];

		assert(len_vnvn(pt, pt_prev, dims) == points_length_cache[i]);
#else
		length = len_vnvn(pt, pt_prev, dims);
#endif

		r_u[i] = r_u[i_prev] + length;
		i_prev = i;
		pt_prev = pt;
		pt += dims;
	}
	assert(!is_almost_zero(r_u[points_offset_len - 1]));
	const double w = r_u[points_offset_len - 1];
	for (uint i = 0; i < points_offset_len; i++) {
		r_u[i] /= w;
	}
}

/**
 * Use Newton-Raphson iteration to find better root.
 *
 * \param cubic: Current fitted curve.
 * \param p: Point to test against.
 * \param u: Parameter value for \a p.
 *
 * \note Return value may be `nan` caller must check for this.
 */
static double cubic_find_root(
		const Cubic *cubic,
		const double p[],
		const double u,
		const uint dims)
{
	/* Newton-Raphson Method. */
	/* all vectors */
#ifdef USE_VLA
	double q0_u[dims];
	double q1_u[dims];
	double q2_u[dims];
#else
	double *q0_u = alloca(sizeof(double) * dims);
	double *q1_u = alloca(sizeof(double) * dims);
	double *q2_u = alloca(sizeof(double) * dims);
#endif

	cubic_calc_point(cubic, u, dims, q0_u);
	cubic_calc_speed(cubic, u, dims, q1_u);
	cubic_calc_acceleration(cubic, u, dims, q2_u);

	/* may divide-by-zero, caller must check for that case */
	/* u - ((q0_u - p) * q1_u) / (q1_u.length_squared() + (q0_u - p) * q2_u) */
	isub_vnvn(q0_u, p, dims);
	return u - dot_vnvn(q0_u, q1_u, dims) /
	       (len_squared_vn(q1_u, dims) + dot_vnvn(q0_u, q2_u, dims));
}

static int compare_double_fn(const void *a_, const void *b_)
{
	const double *a = a_;
	const double *b = b_;
	if      (*a > *b) return  1;
	else if (*a < *b) return -1;
	else              return  0;
}

/**
 * Given set of points and their parameterization, try to find a better parameterization.
 */
static bool cubic_reparameterize(
        const Cubic *cubic,
        const double *points_offset,
        const uint    points_offset_len,
        const double *u,
        const uint    dims,

        double       *r_u_prime)
{
	/*
	 * Recalculate the values of u[] based on the Newton Raphson method
	 */

	const double *pt = points_offset;
	for (uint i = 0; i < points_offset_len; i++, pt += dims) {
		r_u_prime[i] = cubic_find_root(cubic, pt, u[i], dims);
		if (!isfinite(r_u_prime[i])) {
			return false;
		}
	}

	qsort(r_u_prime, points_offset_len, sizeof(double), compare_double_fn);

	if ((r_u_prime[0] < 0.0) ||
	    (r_u_prime[points_offset_len - 1] > 1.0))
	{
		return false;
	}

	assert(r_u_prime[0] >= 0.0);
	assert(r_u_prime[points_offset_len - 1] <= 1.0);
	return true;
}


static void fit_cubic_to_points(
        const double *points_offset,
        const uint    points_offset_len,
#ifdef USE_LENGTH_CACHE
        const double *points_length_cache,
#endif
        const double  tan_l[],
        const double  tan_r[],
        const double  error_threshold,
        const uint    dims,
        /* fill in the list */
        CubicList *clist)
{
	const uint iteration_max = 4;
	const double error_sq = sq(error_threshold);

	Cubic *cubic;

	if (points_offset_len == 2) {
		cubic = cubic_alloc(dims);
		CUBIC_VARS(cubic, dims, p0, p1, p2, p3);

		copy_vnvn(p0, &points_offset[0 * dims], dims);
		copy_vnvn(p3, &points_offset[1 * dims], dims);

		const double dist = len_vnvn(p0, p3, dims) / 3.0;
		msub_vn_vnvn_fl(p1, p0, tan_l, dist, dims);
		madd_vn_vnvn_fl(p2, p3, tan_r, dist, dims);

#ifdef USE_ORIG_INDEX_DATA
		cubic->orig_span = 1;
#endif

		cubic_list_prepend(clist, cubic);
		return;
	}

	double *u = malloc(sizeof(double) * points_offset_len);
	points_calc_coord_length(
	        points_offset, points_offset_len, dims,
#ifdef USE_LENGTH_CACHE
	        points_length_cache,
#endif
	        u);

	cubic = cubic_alloc(dims);

	double error_sq_max;
	uint split_index;

	/* Parameterize points, and attempt to fit curve */
	cubic_from_points(
	        points_offset, points_offset_len, u, tan_l, tan_r, dims, cubic);

	/* Find max deviation of points to fitted curve */
	cubic_calc_error(
	        cubic, points_offset, points_offset_len, u, dims,
	        &error_sq_max, &split_index);

	if (error_sq_max < error_sq) {
		free(u);
		cubic_list_prepend(clist, cubic);
		return;
	}
	else {
		/* If error not too large, try some reparameterization and iteration */
		double *u_prime = malloc(sizeof(double) * points_offset_len);
		for (uint iter = 0; iter < iteration_max; iter++) {
			if (!cubic_reparameterize(
			        cubic, points_offset, points_offset_len, u, dims, u_prime))
			{
				break;
			}

			cubic_from_points(
			        points_offset, points_offset_len, u_prime,
			        tan_l, tan_r, dims, cubic);
			cubic_calc_error(
			        cubic, points_offset, points_offset_len, u_prime, dims,
			        &error_sq_max, &split_index);

			if (error_sq_max < error_sq) {
				free(u_prime);
				free(u);
				cubic_list_prepend(clist, cubic);
				return;
			}

			SWAP(double *, u, u_prime);
		}
		free(u_prime);
	}

	free(u);
	cubic_free(cubic);


	/* Fitting failed -- split at max error point and fit recursively */

	/* Check splinePoint is not an endpoint?
	 *
	 * This assert happens sometimes...
	 * Look into it but disable for now. Campbell! */

	// assert(split_index > 1)
#ifdef USE_VLA
	double tan_center[dims];
#else
	double *tan_center = alloca(sizeof(double) * dims);
#endif

	const double *pt_a = &points_offset[(split_index - 1) * dims];
	const double *pt_b = &points_offset[(split_index + 1) * dims];

	assert(split_index < points_offset_len);
	if (equals_vnvn(pt_a, pt_b, dims)) {
		pt_a += dims;
	}

	/* tan_center = (pt_a - pt_b).normalized() */
	normalize_vn_vnvn(tan_center, pt_a, pt_b, dims);

	fit_cubic_to_points(
	        points_offset, split_index + 1,
#ifdef USE_LENGTH_CACHE
	        points_length_cache,
#endif
	        tan_l, tan_center, error_threshold, dims, clist);
	fit_cubic_to_points(
	        &points_offset[split_index * dims], points_offset_len - split_index,
#ifdef USE_LENGTH_CACHE
	        points_length_cache + split_index,
#endif
	        tan_center, tan_r, error_threshold, dims, clist);

}

/** \} */


/* -------------------------------------------------------------------- */

/** \name External API for Curve-Fitting
 * \{ */

/**
 * Main function:
 *
 * Take an array of 3d points.
 * return the cubic splines
 */
int curve_fit_cubic_to_points_db(
        const double *points,
        const uint    points_len,
        const uint    dims,
        const double  error_threshold,
        const uint   *corners,
        uint          corners_len,

        double **r_cubic_array, uint *r_cubic_array_len,
        uint **r_cubic_orig_index,
        uint **r_corner_index_array, uint *r_corner_index_len)
{
	uint corners_buf[2];
	if (corners == NULL) {
		assert(corners_len == 0);
		corners_buf[0] = 0;
		corners_buf[1] = points_len - 1;
		corners = corners_buf;
		corners_len = 2;
	}

	CubicList clist = {0};
	clist.dims = dims;

#ifdef USE_VLA
	double tan_l[dims];
	double tan_r[dims];
#else
	double *tan_l = alloca(sizeof(double) * dims);
	double *tan_r = alloca(sizeof(double) * dims);
#endif

#ifdef USE_LENGTH_CACHE
	double *points_length_cache = NULL;
	uint    points_length_cache_len_alloc = 0;
#endif

	uint *corner_index_array = NULL;
	uint  corner_index = 0;
	if (r_corner_index_array && (corners != corners_buf)) {
		corner_index_array = malloc(sizeof(uint) * corners_len);
		corner_index_array[corner_index++] = corners[0];
	}

	for (uint i = 1; i < corners_len; i++) {
		const uint points_offset_len = corners[i] - corners[i - 1] + 1;
		const uint first_point = corners[i - 1];

		assert(points_offset_len >= 1);
		if (points_offset_len > 1) {
			const double *pt_l = &points[first_point * dims];
			const double *pt_r = &points[(first_point + points_offset_len - 1) * dims];
			const double *pt_l_next = pt_l + dims;
			const double *pt_r_prev = pt_r - dims;

			/* tan_l = (pt_l - pt_l_next).normalized()
			 * tan_r = (pt_r_prev - pt_r).normalized()
			 */
			normalize_vn_vnvn(tan_l, pt_l, pt_l_next, dims);
			normalize_vn_vnvn(tan_r, pt_r_prev, pt_r, dims);

#ifdef USE_LENGTH_CACHE
			if (points_length_cache_len_alloc < points_offset_len) {
				if (points_length_cache) {
					free(points_length_cache);
				}
				points_length_cache = malloc(sizeof(double) * points_offset_len);
			}
			points_calc_coord_length_cache(
			        &points[first_point * dims], points_offset_len, dims,
			        points_length_cache);
#endif

			fit_cubic_to_points(
			        &points[first_point * dims], points_offset_len,
#ifdef USE_LENGTH_CACHE
			        points_length_cache,
#endif
			        tan_l, tan_r, error_threshold, dims, &clist);
		}
		else if (points_len == 1) {
			assert(points_offset_len == 1);
			assert(corners_len == 2);
			assert(corners[0] == 0);
			assert(corners[1] == 0);
			const double *pt = &points[0];
			Cubic *cubic = cubic_alloc(dims);
			cubic_init(cubic, pt, pt, pt, pt, dims);
			cubic_list_prepend(&clist, cubic);
		}

		if (corner_index_array) {
			corner_index_array[corner_index++] = clist.len;
		}
	}

#ifdef USE_LENGTH_CACHE
	if (points_length_cache) {
		free(points_length_cache);
	}
#endif

#ifdef USE_ORIG_INDEX_DATA
	uint *cubic_orig_index = NULL;
	if (r_cubic_orig_index) {
		cubic_orig_index = malloc(sizeof(uint) * (clist.len + 1));
	}
#else
	*r_cubic_orig_index = NULL;
#endif

	/* allocate a contiguous array and free the linked list */
	*r_cubic_array = cubic_list_as_array(
	        &clist
#ifdef USE_ORIG_INDEX_DATA
	        , corners[corners_len - 1], cubic_orig_index
#endif
	        );
	*r_cubic_array_len = clist.len + 1;

	cubic_list_clear(&clist);

#ifdef USE_ORIG_INDEX_DATA
	if (cubic_orig_index) {
		*r_cubic_orig_index = cubic_orig_index;
	}
#endif

	if (corner_index_array) {
		assert(corner_index == corners_len);
		*r_corner_index_array = corner_index_array;
		*r_corner_index_len = corner_index;
	}

	return 0;
}

/**
 * A version of #curve_fit_cubic_to_points_db to handle floats
 */
int curve_fit_cubic_to_points_fl(
        const float  *points,
        const uint    points_len,
        const uint    dims,
        const float   error_threshold,
        const uint   *corners,
        const uint    corners_len,

        float **r_cubic_array, uint *r_cubic_array_len,
        uint **r_cubic_orig_index,
        uint **r_corner_index_array, uint *r_corner_index_len)
{
	const uint points_flat_len = points_len * dims;
	double *points_db = malloc(sizeof(double) * points_flat_len);

	for (uint i = 0; i < points_flat_len; i++) {
		points_db[i] = (double)points[i];
	}

	double *cubic_array_db = NULL;
	float  *cubic_array_fl = NULL;
	uint    cubic_array_len = 0;

	int result = curve_fit_cubic_to_points_db(
	        points_db, points_len, dims, error_threshold, corners, corners_len,
	        &cubic_array_db, &cubic_array_len,
	        r_cubic_orig_index,
	        r_corner_index_array, r_corner_index_len);
	free(points_db);

	if (!result) {
		uint cubic_array_flat_len = cubic_array_len * 3 * dims;
		cubic_array_fl = malloc(sizeof(float) * cubic_array_flat_len);
		for (uint i = 0; i < cubic_array_flat_len; i++) {
			cubic_array_fl[i] = (float)cubic_array_db[i];
		}
		free(cubic_array_db);
	}

	*r_cubic_array = cubic_array_fl;
	*r_cubic_array_len = cubic_array_len;

	return result;
}

/** \} */

/*
 * GFOpenCalc's portable core: the Solver's linear programme. A dense
 * two-phase simplex (the largest coefficient enters, Bland's rule after a
 * run of steps that gain nothing, so it cannot cycle), bounds on single
 * variables kept out of the tableau where they can be, and branch and bound
 * for integer variables (depth first, the most fractional variable first).
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LP_EPS 1e-9           /* a coefficient this small is 0 */
#define LP_FEAS 1e-7          /* how far a row may be off and still hold */
#define LP_INT 1e-6           /* how far a value may be from a whole number */
#define LP_MAX_DOUBLES 524288u /* the tableau's size, at most (4 MB) */

static int lp_finite(double v)
{
    return v == v && v <= DBL_MAX && v >= -DBL_MAX;
}

/* One relaxation: bounds lower/upper on each variable (HUGE_VAL for none). */
typedef struct {
    const oc_lp *lp;
    unsigned rows, cols, width;  /* tableau rows (constraints), columns (without the right-hand side) */
    unsigned nstruct;            /* columns that stand for variables */
    unsigned art;                /* the first artificial column */
    double *t;                   /* (rows + 1) x width, row 0 the objective */
    unsigned *basis;             /* rows: the column basic in each row */
    int *kind;                   /* per variable: 0 shifted (x = lo + y), 1 mirrored (x = hi - y), 2 free (y+ - y-) */
    unsigned *column;            /* per variable: its (first) column */
    unsigned char *dropped;      /* rows found redundant */
    unsigned long steps;
} lp_tab;

#define T(tb, r, c) ((tb)->t[(size_t)(r) * (tb)->width + (c)])

static void pivot(lp_tab *tb, unsigned pr, unsigned pc)
{
    unsigned r, c, w = tb->width;
    double *prow = &T(tb, pr, 0), p = prow[pc];
    for (c = 0; c < w; c++)
        prow[c] /= p;
    prow[pc] = 1.0;
    for (r = 0; r <= tb->rows; r++) {
        double f, *row;
        if (r == pr)
            continue;
        row = &T(tb, r, 0);
        f = row[pc];
        if (f == 0.0)
            continue;
        for (c = 0; c < w; c++)
            if (prow[c] != 0.0)
                row[c] -= f * prow[c];
        row[pc] = 0.0;
    }
    tb->basis[pr - 1] = pc;
    tb->steps++;
}

/* Maximises row 0 over columns below limit. 0 optimal, 1 unbounded, 2 out of steps. */
static int simplex(lp_tab *tb, unsigned limit)
{
    unsigned rhs = tb->width - 1, stale = 0;
    unsigned long cap = 20000ul + 50ul * (tb->rows + tb->cols);
    for (;;) {
        unsigned pc = (unsigned)-1, pr = 0, r, c;
        double best = -LP_EPS, ratio = 0.0;
        int bland = stale > 50;
        if (tb->steps > cap)
            return 2;
        for (c = 0; c < limit; c++) {
            double d = T(tb, 0, c);
            if (d < best) {
                pc = c;
                if (bland)
                    break;
                best = d;
            }
        }
        if (pc == (unsigned)-1)
            return 0;
        for (r = 1; r <= tb->rows; r++) {
            double a = T(tb, r, pc), q;
            if (tb->dropped[r - 1] || a <= LP_EPS)
                continue;
            q = T(tb, r, rhs) / a;
            if (!pr || q < ratio - LP_EPS ||
                (q <= ratio + LP_EPS && tb->basis[r - 1] < tb->basis[pr - 1])) {
                pr = r;
                ratio = q;
            }
        }
        if (!pr)
            return 1;
        stale = ratio <= LP_EPS ? stale + 1 : 0;
        pivot(tb, pr, pc);
    }
}

/* A row with one variable in it is a bound on that variable: it moves into
 * lower/upper. 0 when that leaves the bounds crossed (infeasible). */
static int rows_to_bounds(const oc_lp *lp, double *lower, double *upper, unsigned char *as_bound)
{
    unsigned i, j;
    for (i = 0; i < lp->m; i++) {
        const double *a = &lp->a[(size_t)i * lp->n];
        unsigned only = (unsigned)-1, count = 0;
        double k, v;
        as_bound[i] = 0;
        for (j = 0; j < lp->n; j++)
            if (fabs(a[j]) > LP_EPS) {
                only = j;
                count++;
            }
        if (count > 1)
            continue;
        if (!count) {
            /* 0 rel b: holds or never does */
            double b = lp->b[i];
            as_bound[i] = 1;
            if ((lp->rel[i] < 0 && b < -LP_FEAS) || (lp->rel[i] > 0 && b > LP_FEAS) ||
                (lp->rel[i] == 0 && fabs(b) > LP_FEAS))
                return 0;
            continue;
        }
        k = a[only];
        v = lp->b[i] / k;
        as_bound[i] = 1;
        if (lp->rel[i] == 0) {
            if (v > lower[only]) lower[only] = v;
            if (v < upper[only]) upper[only] = v;
        } else if ((lp->rel[i] < 0) == (k > 0)) {
            if (v < upper[only]) upper[only] = v;  /* k x <= b, k > 0; or k x >= b, k < 0 */
        } else if (v > lower[only]) {
            lower[only] = v;
        }
    }
    for (j = 0; j < lp->n; j++)
        if (lower[j] > upper[j] + LP_FEAS * (1.0 + fabs(upper[j])))
            return 0;
        else if (lower[j] > upper[j])
            lower[j] = upper[j];
    return 1;
}

/* The relaxation with these bounds: OC_LP_OPTIMAL (x and *objective set),
 * OC_LP_INFEASIBLE, OC_LP_UNBOUNDED, OC_LP_LIMIT (out of steps), OC_LP_TOO_BIG
 * or OC_LP_MEMORY. */
static int relax(const oc_lp *lp, const unsigned char *as_bound, const double *lower, const double *upper,
                 double *x, double *objective, unsigned long *steps)
{
    lp_tab tb;
    unsigned n = lp->n, i, j, r, c, nrows = 0, nslack = 0, nart = 0, k;
    unsigned slack, artn;
    int rc = OC_LP_MEMORY, s;
    size_t need;

    memset(&tb, 0, sizeof tb);
    tb.lp = lp;
    tb.kind = malloc((n ? n : 1) * sizeof *tb.kind);
    tb.column = malloc((n ? n : 1) * sizeof *tb.column);
    if (!tb.kind || !tb.column)
        goto done;
    for (j = 0; j < n; j++) {
        tb.column[j] = tb.nstruct;
        if (lp_finite(lower[j])) {
            tb.kind[j] = 0;
            tb.nstruct++;
            if (lp_finite(upper[j]) && upper[j] - lower[j] > LP_FEAS) {
                nrows++;
                nslack++;
            }
        } else if (lp_finite(upper[j])) {
            tb.kind[j] = 1;
            tb.nstruct++;
        } else {
            tb.kind[j] = 2;
            tb.nstruct += 2;
        }
    }
    /* a variable with lower == upper stays a column (fixed at y = 0 by an upper row it does not get): */
    for (j = 0; j < n; j++)
        if (tb.kind[j] == 0 && lp_finite(upper[j]) && upper[j] - lower[j] <= LP_FEAS) {
            nrows++;
            nart++;
        }
    for (i = 0; i < lp->m; i++)
        if (!as_bound[i]) {
            nrows++;
            if (lp->rel[i] == 0) nart++;
            else { nslack++; nart++; }  /* an artificial only when its right-hand side's sign needs one */
        }
    tb.rows = nrows;
    tb.cols = tb.nstruct + nslack + nart;
    tb.width = tb.cols + 1;
    need = (size_t)(tb.rows + 1) * tb.width;
    if (need > LP_MAX_DOUBLES) {
        rc = OC_LP_TOO_BIG;
        goto done;
    }
    tb.t = calloc(need, sizeof *tb.t);
    tb.basis = calloc(tb.rows ? tb.rows : 1, sizeof *tb.basis);
    tb.dropped = calloc(tb.rows ? tb.rows : 1, 1);
    if (!tb.t || !tb.basis || !tb.dropped)
        goto done;
    slack = tb.nstruct;
    artn = tb.nstruct + nslack;
    tb.art = artn;

    /* fill each row as sum(coef * column) rel rhs, then make rhs >= 0 and give it a basic column */
    r = 0;
    for (k = 0; k < lp->m + 2 * n; k++) {
        int rel;
        double rhs, *row;
        unsigned use;
        if (k < lp->m) {
            if (as_bound[k])
                continue;
            row = &T(&tb, r + 1, 0);
            rel = lp->rel[k];
            rhs = lp->b[k];
            for (j = 0; j < n; j++) {
                double a = lp->a[(size_t)k * n + j];
                if (a == 0.0)
                    continue;
                if (tb.kind[j] == 0) { row[tb.column[j]] += a; rhs -= a * lower[j]; }
                else if (tb.kind[j] == 1) { row[tb.column[j]] -= a; rhs -= a * upper[j]; }
                else { row[tb.column[j]] += a; row[tb.column[j] + 1] -= a; }
            }
        } else if (k < lp->m + n) {
            j = k - lp->m;
            if (tb.kind[j] != 0 || !lp_finite(upper[j]) || upper[j] - lower[j] <= LP_FEAS)
                continue;
            row = &T(&tb, r + 1, 0);
            rel = -1;
            rhs = upper[j] - lower[j];
            row[tb.column[j]] = 1.0;
        } else {
            j = k - lp->m - n;
            if (tb.kind[j] != 0 || !lp_finite(upper[j]) || upper[j] - lower[j] > LP_FEAS)
                continue;
            row = &T(&tb, r + 1, 0);
            rel = 0;
            rhs = 0.0;
            row[tb.column[j]] = 1.0;
        }
        if (rhs < 0.0) {
            for (c = 0; c < tb.nstruct; c++)
                row[c] = -row[c];
            rhs = -rhs;
            rel = -rel;
        }
        if (rel < 0) {
            row[slack] = 1.0;
            use = slack++;
        } else if (rel > 0) {
            row[slack++] = -1.0;
            row[artn] = 1.0;
            use = artn++;
        } else {
            row[artn] = 1.0;
            use = artn++;
        }
        row[tb.width - 1] = rhs;
        tb.basis[r] = use;
        r++;
    }
    tb.rows = r;

    /* phase 1: maximise -(sum of artificials) */
    for (r = 1; r <= tb.rows; r++)
        if (tb.basis[r - 1] >= tb.art)
            for (c = 0; c < tb.width; c++)
                if (c < tb.art || c == tb.width - 1)
                    T(&tb, 0, c) -= T(&tb, r, c);
    s = simplex(&tb, artn);
    if (s == 2) { rc = OC_LP_LIMIT; goto done; }
    if (T(&tb, 0, tb.width - 1) < -LP_FEAS * (1.0 + tb.rows)) {   /* -(sum of artificials) left over */
        rc = OC_LP_INFEASIBLE;
        goto done;
    }
    /* artificials still basic (at 0): pivot them out, or drop their row as redundant */
    for (r = 1; r <= tb.rows; r++) {
        if (tb.basis[r - 1] < tb.art)
            continue;
        for (c = 0; c < tb.art; c++)
            if (fabs(T(&tb, r, c)) > LP_EPS)
                break;
        if (c < tb.art)
            pivot(&tb, r, c);
        else
            tb.dropped[r - 1] = 1;
    }

    /* phase 2: maximise c.x (minus its constant part) over the columns that are not artificial */
    for (c = 0; c < tb.width; c++)
        T(&tb, 0, c) = 0.0;
    for (j = 0; j < n; j++) {
        double cj = lp->c[j];
        if (tb.kind[j] == 0) T(&tb, 0, tb.column[j]) = -cj;
        else if (tb.kind[j] == 1) T(&tb, 0, tb.column[j]) = cj;
        else { T(&tb, 0, tb.column[j]) = -cj; T(&tb, 0, tb.column[j] + 1) = cj; }
    }
    for (r = 1; r <= tb.rows; r++) {
        unsigned b = tb.basis[r - 1];
        double f = T(&tb, 0, b);
        if (tb.dropped[r - 1] || f == 0.0)
            continue;
        for (c = 0; c < tb.width; c++)
            T(&tb, 0, c) -= f * T(&tb, r, c);
    }
    s = simplex(&tb, tb.art);
    if (s == 2) { rc = OC_LP_LIMIT; goto done; }
    if (s == 1) { rc = OC_LP_UNBOUNDED; goto done; }

    {
        double *y = calloc(tb.cols ? tb.cols : 1, sizeof *y);
        if (!y)
            goto done;
        for (r = 1; r <= tb.rows; r++)
            if (!tb.dropped[r - 1])
                y[tb.basis[r - 1]] = T(&tb, r, tb.width - 1);
        *objective = 0.0;
        for (j = 0; j < n; j++) {
            double v;
            if (tb.kind[j] == 0) v = lower[j] + y[tb.column[j]];
            else if (tb.kind[j] == 1) v = upper[j] - y[tb.column[j]];
            else v = y[tb.column[j]] - y[tb.column[j] + 1];
            x[j] = v;
            *objective += lp->c[j] * v;
        }
        free(y);
    }
    rc = OC_LP_OPTIMAL;
done:
    if (steps)
        *steps += tb.steps;
    free(tb.t);
    free(tb.basis);
    free(tb.dropped);
    free(tb.kind);
    free(tb.column);
    return rc;
}

typedef struct {
    double *lower, *upper;
} lp_node;

int oc_lp_solve(const oc_lp *lp, double *x, double *objective, unsigned *nodes_out)
{
    unsigned n, j, nodes = 0, sp = 0, cap, max_nodes;
    unsigned char *as_bound = NULL;
    double *lower = NULL, *upper = NULL, *xr = NULL, best = -HUGE_VAL, obj;
    lp_node *stack = NULL;
    unsigned long steps = 0;
    int rc = OC_LP_MEMORY, found = 0, any_int = 0, limit = 0, r;

    if (!lp || !x || !objective || !lp->n)
        return OC_LP_INFEASIBLE;
    n = lp->n;
    max_nodes = lp->max_nodes ? lp->max_nodes : 5000u;
    as_bound = malloc(lp->m ? lp->m : 1);
    lower = malloc(n * sizeof *lower);
    upper = malloc(n * sizeof *upper);
    xr = malloc(n * sizeof *xr);
    if (!as_bound || !lower || !upper || !xr)
        goto done;
    for (j = 0; j < n; j++) {
        lower[j] = lp->lower ? lp->lower[j] : 0.0;
        upper[j] = lp->upper ? lp->upper[j] : HUGE_VAL;
        if (lp->integer && lp->integer[j]) {
            any_int = 1;
            if (lp_finite(lower[j])) lower[j] = ceil(lower[j] - LP_INT);
            if (lp_finite(upper[j])) upper[j] = floor(upper[j] + LP_INT);
        }
    }
    if (!rows_to_bounds(lp, lower, upper, as_bound)) {
        rc = OC_LP_INFEASIBLE;
        goto done;
    }
    if (any_int)
        for (j = 0; j < n; j++)
            if (lp->integer && lp->integer[j]) {
                if (lp_finite(lower[j])) lower[j] = ceil(lower[j] - LP_INT);
                if (lp_finite(upper[j])) upper[j] = floor(upper[j] + LP_INT);
                if (lower[j] > upper[j]) { rc = OC_LP_INFEASIBLE; goto done; }
            }
    if (!any_int) {
        rc = relax(lp, as_bound, lower, upper, x, objective, &steps);
        if (rc == OC_LP_LIMIT)
            rc = OC_LP_LIMIT_NONE;
        nodes = 1;
        goto done;
    }

    /* branch and bound */
    cap = 64;
    stack = malloc(cap * sizeof *stack);
    if (!stack)
        goto done;
    stack[0].lower = lower;
    stack[0].upper = upper;
    lower = upper = NULL;
    sp = 1;
    while (sp) {
        lp_node nd = stack[--sp];
        unsigned pick = (unsigned)-1;
        double frac = 0.0;
        if (nodes >= max_nodes) {
            limit = 1;
            free(nd.lower);
            free(nd.upper);
            continue;
        }
        nodes++;
        r = relax(lp, as_bound, nd.lower, nd.upper, xr, &obj, &steps);
        if (r == OC_LP_MEMORY || r == OC_LP_TOO_BIG) {
            free(nd.lower); free(nd.upper);
            rc = r;
            goto done;
        }
        if (r == OC_LP_UNBOUNDED && !found && nodes == 1) {
            free(nd.lower); free(nd.upper);
            rc = OC_LP_UNBOUNDED;
            goto done;
        }
        if (r == OC_LP_LIMIT)
            limit = 1;
        if (r != OC_LP_OPTIMAL || (found && obj <= best + LP_FEAS * (1.0 + fabs(best)))) {
            free(nd.lower); free(nd.upper);
            continue;
        }
        for (j = 0; j < n; j++)
            if (lp->integer[j]) {
                double f = xr[j] - floor(xr[j]);
                double d = f < 0.5 ? f : 1.0 - f;
                if (d > LP_INT && d > frac) {
                    frac = d;
                    pick = j;
                }
            }
        if (pick == (unsigned)-1) {
            found = 1;
            best = obj;
            for (j = 0; j < n; j++)
                x[j] = lp->integer[j] ? floor(xr[j] + 0.5) : xr[j];
            free(nd.lower); free(nd.upper);
            continue;
        }
        if (sp + 2 > cap) {
            lp_node *more = realloc(stack, (cap * 2) * sizeof *stack);
            if (!more) { free(nd.lower); free(nd.upper); goto done; }
            stack = more;
            cap *= 2;
        }
        {
            double v = xr[pick], *lo2 = malloc(n * sizeof *lo2), *up2 = malloc(n * sizeof *up2);
            int up_first = v - floor(v) >= 0.5;   /* the nearer side is searched first: pushed last */
            if (!lo2 || !up2) { free(lo2); free(up2); free(nd.lower); free(nd.upper); goto done; }
            memcpy(lo2, nd.lower, n * sizeof *lo2);
            memcpy(up2, nd.upper, n * sizeof *up2);
            /* nd becomes x <= floor(v); the copy x >= ceil(v) */
            nd.upper[pick] = floor(v);
            lo2[pick] = ceil(v);
            if (up_first) {
                stack[sp].lower = nd.lower; stack[sp].upper = nd.upper; sp++;
                stack[sp].lower = lo2; stack[sp].upper = up2; sp++;
            } else {
                stack[sp].lower = lo2; stack[sp].upper = up2; sp++;
                stack[sp].lower = nd.lower; stack[sp].upper = nd.upper; sp++;
            }
        }
    }
    if (found) {
        *objective = best;
        rc = limit ? OC_LP_LIMIT : OC_LP_OPTIMAL;
    } else {
        rc = limit ? OC_LP_LIMIT_NONE : OC_LP_INFEASIBLE;
    }
done:
    while (sp) {
        sp--;
        free(stack[sp].lower);
        free(stack[sp].upper);
    }
    free(stack);
    free(as_bound);
    free(lower);
    free(upper);
    free(xr);
    if (nodes_out)
        *nodes_out = nodes;
    return rc;
}

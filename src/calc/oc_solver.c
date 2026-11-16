/*
 * GFOpenCalc's portable core: Solver. The model (a target cell to make as
 * large or small as it can be, or equal to a value, by changing variable
 * cells, under constraints) is read off the sheet as a linear programme on a
 * private copy of the book: the formulas are worked out at the variables'
 * values, then with each variable one more, and the model is checked at two
 * more points so a formula that is not linear is reported, not solved
 * wrongly. oc_lp.c solves it. The live book changes only when the answer is
 * applied, as one Undo step.
 *
 * The model is kept on its sheet in hidden names, as Excel keeps its Solver
 * models (solver_opt, solver_typ, solver_val, solver_adj, solver_num,
 * solver_lhsN, solver_relN, solver_rhsN, solver_neg), so a model made in
 * either reads in the other.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SOLVER_PROBES 4096u    /* cells a model reads: the target, constraints' sides */

static int fin(double v)
{
    return v == v && v <= DBL_MAX && v >= -DBL_MAX;
}

static void why(oc_solver_result *res, const char *fmt, const char *ref)
{
    char shown[96];
    size_t n = ref ? strlen(ref) : 0;
    if (n >= sizeof shown)
        n = sizeof shown - 1;
    memcpy(shown, ref ? ref : "", n);
    shown[n] = 0;
    snprintf(res->why, sizeof res->why, fmt, shown);
}

/* A number written as text: "12", " -3.5 ", "=4". 1 when the whole text is one. */
static int number_text(const char *text, double *v)
{
    char *end;
    while (*text == ' ' || *text == '=')
        text++;
    if (!*text)
        return 0;
    *v = strtod(text, &end);
    while (*end == ' ')
        end++;
    return !*end && fin(*v);
}

typedef struct {
    int sheet;
    unsigned row, col;
} cell_at;

typedef struct {
    const oc_core *core;
    owf_book *book;             /* the private copy */
    owf_calc *calc;
    int here;
    cell_at var[OC_SOLVER_VARIABLES];
    unsigned nvar;
    double x0[OC_SOLVER_VARIABLES];
    cell_at *probe;             /* cells read: 0 the target, then each constraint's cells */
    unsigned nprobe;
    struct { unsigned lhs, rhs, count; int rhs_one, rhs_cells; double rhs_value; } con[OC_SOLVER_CONSTRAINTS];
} solver;

static int find_var(const solver *s, int sheet, unsigned row, unsigned col)
{
    unsigned j;
    for (j = 0; j < s->nvar; j++)
        if (s->var[j].sheet == sheet && s->var[j].row == row && s->var[j].col == col)
            return (int)j;
    return -1;
}

static int add_probes(solver *s, int sheet, const owf_range *r)
{
    unsigned row, col;
    unsigned long n = (unsigned long)(r->last_row - r->first_row + 1u) * (r->last_col - r->first_col + 1u);
    if (n > SOLVER_PROBES - s->nprobe)
        return 0;
    for (row = r->first_row; row <= r->last_row; row++)
        for (col = r->first_col; col <= r->last_col; col++) {
            s->probe[s->nprobe].sheet = sheet;
            s->probe[s->nprobe].row = row;
            s->probe[s->nprobe].col = col;
            s->nprobe++;
        }
    return 1;
}

/* The variable cells from "B2:C2,'Sheet 2'!E4" (commas or semicolons between ranges). */
static int read_vars(solver *s, const char *text, oc_solver_result *res)
{
    char part[OC_SOLVER_REF];
    const char *p = text;
    while (*p) {
        size_t n = 0;
        int quoted = 0, sheet;
        owf_range r;
        unsigned row, col;
        while (*p == ' ' || *p == ',' || *p == ';')
            p++;
        if (!*p)
            break;
        while (*p && (quoted || (*p != ',' && *p != ';'))) {
            if (*p == '\'')
                quoted = !quoted;
            if (n + 1 >= sizeof part)
                return why(res, "The variable cells are too long a list.", NULL), 0;
            part[n++] = *p++;
        }
        while (n && part[n - 1] == ' ')
            n--;
        part[n] = 0;
        if (!oc_sheet_range(s->book, part, s->here, &sheet, &r))
            return why(res, "\"%s\" is not a cell or range for the variables.", part), 0;
        for (row = r.first_row; row <= r.last_row; row++)
            for (col = r.first_col; col <= r.last_col; col++) {
                const owf_cell *cell = owf_sheet_find(&s->book->sheets[sheet], row, col);
                char ref[16];
                if (find_var(s, sheet, row, col) >= 0)
                    continue;
                oc_cell_name(row, col, ref);
                if (s->nvar >= OC_SOLVER_VARIABLES)
                    return why(res, "Solver takes at most 200 variable cells.", NULL), 0;
                if (cell && (cell->formula || (cell->type != OWF_VALUE_EMPTY && cell->type != OWF_VALUE_NUMBER)))
                    return why(res, "Variable cell %s must be empty or a number, not a formula or text.", ref), 0;
                s->var[s->nvar].sheet = sheet;
                s->var[s->nvar].row = row;
                s->var[s->nvar].col = col;
                s->x0[s->nvar] = cell && cell->type == OWF_VALUE_NUMBER && fin(cell->number) ? cell->number : 0.0;
                s->nvar++;
            }
    }
    if (!s->nvar)
        return why(res, "Give the variable cells, such as B2:B5.", NULL), 0;
    return 1;
}

static int set_vars(solver *s, const double *x)
{
    unsigned j;
    char text[48];
    for (j = 0; j < s->nvar; j++) {
        int rc;
        owf_number_text(x[j], text);
        rc = owf_calc_enter(s->calc, s->var[j].sheet, s->var[j].row, s->var[j].col, text);
        if (rc != OWF_OK)
            return rc;
    }
    return owf_calc_recalc(s->calc);
}

/* The probes' values now; 0 (and *bad set) when one is not a number. */
static int read_probes(solver *s, double *out, unsigned *bad)
{
    unsigned i;
    for (i = 0; i < s->nprobe; i++) {
        const owf_cell *cell = owf_sheet_find(&s->book->sheets[s->probe[i].sheet], s->probe[i].row, s->probe[i].col);
        if (!cell || cell->type == OWF_VALUE_EMPTY)
            out[i] = 0.0;
        else if (cell->type == OWF_VALUE_NUMBER && fin(cell->number))
            out[i] = cell->number;
        else if (cell->type == OWF_VALUE_BOOL)
            out[i] = cell->number != 0.0;
        else {
            *bad = i;
            return 0;
        }
    }
    return 1;
}

static void probe_name(const solver *s, unsigned i, char *out)
{
    oc_cell_name(s->probe[i].row, s->probe[i].col, out);
}

/* ---- the Nonlinear engine: Nelder and Mead's simplex search, the constraints as penalties ---- */

#define NL_TRIALS 4000u

typedef struct {
    solver *s;
    const oc_solver_model *model;
    double *p;                  /* the probes at the point last worked out */
    double weight;              /* the penalty's weight now */
    unsigned trials, limit;
    int stopped;                /* the limit reached */
    int failed;                 /* OWF_ERR_... from the engine, 0 none */
} nl_run;

/* x kept to the variables' limits (>= 0 when the model says so). */
static void nl_keep(const nl_run *r, double *x)
{
    unsigned j;
    if (r->model->nonnegative)
        for (j = 0; j < r->s->nvar; j++)
            if (x[j] < 0.0)
                x[j] = 0.0;
}

/* How far the probes p break the constraints (and a value goal): the sum of the squares. */
static double nl_breach(const nl_run *r, const double *p)
{
    const solver *s = r->s;
    const oc_solver_model *m = r->model;
    double sum = 0.0;
    unsigned k, i;
    for (k = 0; k < m->nconstraints; k++) {
        int rel = m->constraints[k].rel;
        if (rel > OC_SOLVER_GE)
            continue;
        for (i = 0; i < s->con[k].count; i++) {
            double l = p[s->con[k].lhs + i];
            double rv = s->con[k].rhs_cells ? p[s->con[k].rhs + (s->con[k].rhs_one ? 0u : i)] : s->con[k].rhs_value;
            double d = rel == OC_SOLVER_LE ? (l > rv ? l - rv : 0.0) : rel == OC_SOLVER_GE ? (l < rv ? rv - l : 0.0) : l - rv;
            sum += d * d;
        }
    }
    if (m->goal == OC_SOLVER_VALUE)
        sum += (p[0] - m->value) * (p[0] - m->value);
    return sum;
}

/* The search's measure at x: the target (made smaller), plus the weighted breach. HUGE_VAL where a cell the
 * model reads is not a number. */
static double nl_value(nl_run *r, double *x)
{
    unsigned bad;
    double t;
    nl_keep(r, x);
    if (r->trials >= r->limit) {
        r->stopped = 1;
        return HUGE_VAL;
    }
    r->trials++;
    if (set_vars(r->s, x) != OWF_OK) {
        r->failed = OWF_ERR_MEMORY;
        return HUGE_VAL;
    }
    if (!read_probes(r->s, r->p, &bad))
        return HUGE_VAL;
    t = r->model->goal == OC_SOLVER_MAX ? -r->p[0] : r->model->goal == OC_SOLVER_MIN ? r->p[0] : 0.0;
    return t + r->weight * nl_breach(r, r->p);
}

/* One Nelder-Mead search from x (its answer back in x, its measure returned); v holds n + 1 points of n. */
static double nl_search(nl_run *r, double *x, double *v, double *f, double *c, double *t1, double *t2)
{
    unsigned n = r->s->nvar, i, j;
    for (i = 0; i <= n; i++) {
        double *pt = &v[(size_t)i * n];
        memcpy(pt, x, n * sizeof *pt);
        if (i) {
            double step = fabs(x[i - 1]) > 1e-9 ? 0.1 * fabs(x[i - 1]) : 0.5;
            pt[i - 1] += step;
            if (r->model->nonnegative && pt[i - 1] < 0.0)
                pt[i - 1] = x[i - 1] + step;
        }
        f[i] = nl_value(r, pt);
    }
    while (!r->stopped && !r->failed) {
        unsigned best = 0, worst = 0, next;
        double size = 0.0, scale = 0.0, fr, fe, fc;
        for (i = 1; i <= n; i++) {
            if (f[i] < f[best]) best = i;
            if (f[i] > f[worst]) worst = i;
        }
        next = worst == best ? (best + 1) % (n + 1) : best;
        for (i = 0; i <= n; i++)
            if (i != worst && f[i] > f[next]) next = i;
        /* stop when the points have come together and agree */
        for (i = 0; i <= n; i++)
            for (j = 0; j < n; j++) {
                double d = fabs(v[(size_t)i * n + j] - v[(size_t)best * n + j]);
                if (d > size) size = d;
                if (fabs(v[(size_t)best * n + j]) > scale) scale = fabs(v[(size_t)best * n + j]);
            }
        if (f[best] < HUGE_VAL && size <= 1e-10 * (1.0 + scale) && fabs(f[worst] - f[best]) <= 1e-12 * (1.0 + fabs(f[best])))
            break;
        if (size <= 1e-14 * (1.0 + scale))
            break;
        /* the centre of the others, and the worst point reflected through it */
        for (j = 0; j < n; j++) {
            double sum = 0.0;
            for (i = 0; i <= n; i++)
                if (i != worst) sum += v[(size_t)i * n + j];
            c[j] = sum / (double)n;
            t1[j] = c[j] + (c[j] - v[(size_t)worst * n + j]);
        }
        fr = nl_value(r, t1);
        if (fr < f[best]) {
            for (j = 0; j < n; j++)
                t2[j] = c[j] + 2.0 * (c[j] - v[(size_t)worst * n + j]);
            fe = nl_value(r, t2);
            if (fe < fr) { memcpy(&v[(size_t)worst * n], t2, n * sizeof *t2); f[worst] = fe; }
            else { memcpy(&v[(size_t)worst * n], t1, n * sizeof *t1); f[worst] = fr; }
            continue;
        }
        if (fr < f[next]) {
            memcpy(&v[(size_t)worst * n], t1, n * sizeof *t1);
            f[worst] = fr;
            continue;
        }
        /* contract, outside or inside */
        for (j = 0; j < n; j++)
            t2[j] = fr < f[worst] ? c[j] + 0.5 * (t1[j] - c[j]) : c[j] + 0.5 * (v[(size_t)worst * n + j] - c[j]);
        fc = nl_value(r, t2);
        if (fc < (fr < f[worst] ? fr : f[worst])) {
            memcpy(&v[(size_t)worst * n], t2, n * sizeof *t2);
            f[worst] = fc;
            continue;
        }
        /* shrink towards the best */
        for (i = 0; i <= n; i++) {
            if (i == best) continue;
            for (j = 0; j < n; j++)
                v[(size_t)i * n + j] = v[(size_t)best * n + j] + 0.5 * (v[(size_t)i * n + j] - v[(size_t)best * n + j]);
            f[i] = nl_value(r, &v[(size_t)i * n]);
        }
    }
    {
        unsigned best = 0;
        for (i = 1; i <= n; i++)
            if (f[i] < f[best]) best = i;
        memcpy(x, &v[(size_t)best * n], n * sizeof *x);
        return f[best];
    }
}

/* Solves s's model with the Nonlinear engine: res filled as oc_core_solve's. */
static int solve_nonlinear(solver *s, const oc_solver_model *model, oc_solver_result *res)
{
    nl_run r;
    unsigned n = s->nvar, j, k, bad = 0, round;
    double *x = NULL, *v = NULL, *f = NULL, *c = NULL, *t1 = NULL, *t2 = NULL, *last = NULL;
    double breach, measure, tol;
    char ref[16];
    int rc = OWF_ERR_MEMORY;
    for (k = 0; k < model->nconstraints; k++)
        if (model->constraints[k].rel == OC_SOLVER_INT || model->constraints[k].rel == OC_SOLVER_BIN) {
            why(res, "The Nonlinear engine takes no whole or binary variables: use Simplex LP for %s.",
                model->constraints[k].lhs);
            return OWF_ERR_FORMAT;
        }
    memset(&r, 0, sizeof r);
    r.s = s;
    r.model = model;
    r.limit = model->max_trials ? model->max_trials : NL_TRIALS;
    r.p = malloc(s->nprobe * sizeof *r.p);
    x = malloc(n * sizeof *x);
    last = malloc(n * sizeof *last);
    v = malloc((size_t)(n + 1) * n * sizeof *v);
    f = malloc((n + 1) * sizeof *f);
    c = malloc(n * sizeof *c);
    t1 = malloc(n * sizeof *t1);
    t2 = malloc(n * sizeof *t2);
    if (!r.p || !x || !last || !v || !f || !c || !t1 || !t2)
        goto done;
    memcpy(x, s->x0, n * sizeof *x);
    nl_keep(&r, x);
    /* the penalty grows each round, the search starting again from the last answer */
    r.weight = 10.0;
    for (round = 0; round < 12 && !r.stopped && !r.failed; round++) {
        memcpy(last, x, n * sizeof *x);
        measure = nl_search(&r, x, v, f, c, t1, t2);
        if (measure >= HUGE_VAL)
            break;
        if (set_vars(s, x) != OWF_OK || !read_probes(s, r.p, &bad))
            break;
        breach = nl_breach(&r, r.p);
        if (fabs(r.p[0]) > 1e30) {
            res->status = OC_SOLVER_UNBOUNDED;
            why(res, "The target has no limit: add constraints that bound it.", NULL);
            rc = OWF_OK;
            goto done;
        }
        if (breach <= 1e-18 && round > 0) {
            double moved = 0.0;
            for (j = 0; j < n; j++)
                if (fabs(x[j] - last[j]) > moved) moved = fabs(x[j] - last[j]);
            if (moved <= 1e-9)
                break;
        }
        r.weight *= 100.0;
    }
    if (r.failed) {
        rc = r.failed;
        goto done;
    }
    res->nodes = r.trials;
    /* the answer, tidied, and worked out once more on the sheet */
    for (j = 0; j < n; j++) {
        double w = floor(x[j] + 0.5);
        if (fabs(x[j] - w) <= 1e-9 * (1.0 + fabs(x[j])))
            x[j] = w;
        if (x[j] == 0.0)
            x[j] = 0.0;
        res->sheet[j] = s->var[j].sheet;
        res->row[j] = s->var[j].row;
        res->col[j] = s->var[j].col;
        res->value[j] = x[j];
    }
    res->nvariables = n;
    rc = OWF_ERR_MEMORY;
    if (set_vars(s, x) != OWF_OK)
        goto done;
    rc = OWF_OK;
    if (!read_probes(s, r.p, &bad)) {
        oc_cell_name(s->probe[bad].row, s->probe[bad].col, ref);
        why(res, "%s is not a number near the answer, so Solver cannot use it.", ref);
        res->status = OC_SOLVER_NO_ANSWER;
        goto done;
    }
    res->objective = r.p[0];
    breach = nl_breach(&r, r.p);
    tol = 1e-6 * (1.0 + fabs(r.p[0]));
    if (breach > tol * tol) {
        res->status = r.stopped ? OC_SOLVER_NO_ANSWER : OC_SOLVER_INFEASIBLE;
        why(res, r.stopped ? "Solver stopped at its limit before it found values that meet every constraint."
                           : "Solver found no values that meet every constraint near the variables' values.", NULL);
        goto done;
    }
    res->status = r.stopped ? OC_SOLVED_LIMIT : OC_SOLVED;
done:
    free(r.p); free(x); free(last); free(v); free(f); free(c); free(t1); free(t2);
    return rc;
}

int oc_core_solve(const oc_core *core, const oc_solver_model *model, oc_solver_result *res)
{
    solver *s = NULL;
    double *p0 = NULL, *coef = NULL, *pt = NULL, *d = NULL, *x = NULL;
    double *a = NULL, *b = NULL, *c = NULL, *lower = NULL, *upper = NULL;
    int *rel = NULL;
    unsigned char *integer = NULL;
    unsigned i, j, k, m = 0, rows = 0, bad = 0, nodes = 0;
    int rc = OWF_ERR_FORMAT, sheet, lp;
    owf_range r;
    double objective = 0.0;
    char ref[16];

    if (!res)
        return OWF_ERR_FORMAT;
    memset(res, 0, sizeof *res);
    res->status = OC_SOLVER_ERROR;
    if (!core || !model)
        return OWF_ERR_FORMAT;
    if (model->goal < OC_SOLVER_MAX || model->goal > OC_SOLVER_VALUE ||
        (model->engine != OC_SOLVER_SIMPLEX && model->engine != OC_SOLVER_NONLINEAR) ||
        (model->goal == OC_SOLVER_VALUE && !fin(model->value)) || model->nconstraints > OC_SOLVER_CONSTRAINTS) {
        why(res, "The model's goal is not one Solver knows.", NULL);
        return OWF_ERR_FORMAT;
    }
    s = calloc(1, sizeof *s);
    if (!s)
        return OWF_ERR_MEMORY;
    s->core = core;
    s->here = core->sheet;
    s->probe = malloc(SOLVER_PROBES * sizeof *s->probe);
    s->book = oc_book_copy(core->book);
    if (!s->probe || !s->book) { rc = OWF_ERR_MEMORY; goto done; }
    s->calc = owf_calc_new(s->book);
    if (!s->calc) { rc = OWF_ERR_MEMORY; goto done; }
    rc = owf_calc_recalc_all(s->calc);
    if (rc != OWF_OK) goto done;
    rc = OWF_ERR_FORMAT;

    /* the target: one cell holding a formula */
    if (!oc_sheet_range(s->book, model->target, s->here, &sheet, &r) || r.first_row != r.last_row ||
        r.first_col != r.last_col) {
        why(res, "The target must be one cell, such as B10.", NULL);
        goto done;
    }
    {
        const owf_cell *t = owf_sheet_find(&s->book->sheets[sheet], r.first_row, r.first_col);
        oc_cell_name(r.first_row, r.first_col, ref);
        if (!t || !t->formula) {
            why(res, "The target cell %s must hold a formula.", ref);
            goto done;
        }
    }
    add_probes(s, sheet, &r);
    if (!read_vars(s, model->variables, res))
        goto done;
    if (find_var(s, sheet, r.first_row, r.first_col) >= 0) {
        why(res, "The target cell %s cannot be a variable cell too.", ref);
        goto done;
    }

    /* each constraint: its cells, and its right-hand side (a number, one cell, or as many cells) */
    for (k = 0; k < model->nconstraints; k++) {
        const oc_solver_constraint *cn = &model->constraints[k];
        int rs;
        owf_range rr;
        unsigned count;
        if (cn->rel < OC_SOLVER_LE || cn->rel > OC_SOLVER_BIN) {
            why(res, "Constraint %s has no relation Solver knows.", cn->lhs);
            goto done;
        }
        if (!oc_sheet_range(s->book, cn->lhs, s->here, &sheet, &r)) {
            why(res, "\"%s\" is not a cell or range for a constraint.", cn->lhs);
            goto done;
        }
        count = (r.last_row - r.first_row + 1u) * (r.last_col - r.first_col + 1u);
        s->con[k].lhs = s->nprobe;
        s->con[k].count = count;
        if (cn->rel == OC_SOLVER_INT || cn->rel == OC_SOLVER_BIN) {
            unsigned row, col;
            for (row = r.first_row; row <= r.last_row; row++)
                for (col = r.first_col; col <= r.last_col; col++)
                    if (find_var(s, sheet, row, col) < 0) {
                        oc_cell_name(row, col, ref);
                        why(res, "%s is whole or binary but is not a variable cell.", ref);
                        goto done;
                    }
        }
        if (!add_probes(s, sheet, &r)) {
            why(res, "The model reads too many cells (4096 at most).", NULL);
            goto done;
        }
        if (cn->rel == OC_SOLVER_INT || cn->rel == OC_SOLVER_BIN)
            continue;
        if (number_text(cn->rhs, &s->con[k].rhs_value))
            continue;
        if (!oc_sheet_range(s->book, cn->rhs, s->here, &rs, &rr)) {
            why(res, "\"%s\" is not a number, cell or range for a constraint's limit.", cn->rhs);
            goto done;
        }
        s->con[k].rhs_cells = 1;
        s->con[k].rhs = s->nprobe;
        s->con[k].rhs_one = rr.first_row == rr.last_row && rr.first_col == rr.last_col;
        if (!s->con[k].rhs_one &&
            (rr.last_row - rr.first_row + 1u) * (rr.last_col - rr.first_col + 1u) != count) {
            why(res, "Constraint %s and its limit are not the same size.", cn->lhs);
            goto done;
        }
        if (!add_probes(s, rs, &rr)) {
            why(res, "The model reads too many cells (4096 at most).", NULL);
            goto done;
        }
    }

    if (model->engine == OC_SOLVER_NONLINEAR) {
        rc = solve_nonlinear(s, model, res);
        goto done;
    }

    /* the model as linear functions of the variables */
    rc = OWF_ERR_MEMORY;
    p0 = malloc(s->nprobe * sizeof *p0);
    pt = malloc(s->nprobe * sizeof *pt);
    coef = malloc((size_t)s->nprobe * s->nvar * sizeof *coef);
    d = malloc(s->nvar * sizeof *d);
    x = malloc(s->nvar * sizeof *x);
    if (!p0 || !pt || !coef || !d || !x)
        goto done;
    if ((rc = set_vars(s, s->x0)) != OWF_OK)
        goto done;
    if (!read_probes(s, p0, &bad))
        goto unreadable;
    memcpy(x, s->x0, s->nvar * sizeof *x);
    for (j = 0; j < s->nvar; j++) {
        x[j] = s->x0[j] + 1.0;
        if ((rc = set_vars(s, x)) != OWF_OK)
            goto done;
        x[j] = s->x0[j];
        if (!read_probes(s, pt, &bad))
            goto unreadable;
        for (i = 0; i < s->nprobe; i++)
            coef[(size_t)i * s->nvar + j] = pt[i] - p0[i];
    }
    for (k = 0; k < 2; k++) {
        for (j = 0; j < s->nvar; j++) {
            d[j] = k ? -(2.0 + 0.29 * (double)(j % 3)) : 1.5 + 0.37 * (double)(j % 5);
            x[j] = s->x0[j] + d[j];
        }
        if ((rc = set_vars(s, x)) != OWF_OK)
            goto done;
        if (!read_probes(s, pt, &bad))
            goto unreadable;
        for (i = 0; i < s->nprobe; i++) {
            double want = p0[i], scale = fabs(p0[i]) + fabs(pt[i]) + 1.0;
            for (j = 0; j < s->nvar; j++) {
                double t = coef[(size_t)i * s->nvar + j] * d[j];
                want += t;
                scale += fabs(t);
            }
            if (fabs(want - pt[i]) > 1e-7 * scale) {
                probe_name(s, i, ref);
                why(res, "The model is not linear: %s does not change in proportion to the variables. "
                         "Choose the Nonlinear engine for it.", ref);
                res->status = OC_SOLVER_NOT_LINEAR;
                rc = OWF_OK;
                goto done;
            }
        }
    }

    /* the linear programme */
    for (k = 0; k < model->nconstraints; k++)
        if (model->constraints[k].rel <= OC_SOLVER_GE)
            m += s->con[k].count;
        else if (model->constraints[k].rel == OC_SOLVER_BIN)
            m += 2 * s->con[k].count;
    if (model->goal == OC_SOLVER_VALUE)
        m++;
    a = calloc((size_t)(m ? m : 1) * s->nvar, sizeof *a);
    b = calloc(m ? m : 1, sizeof *b);
    rel = calloc(m ? m : 1, sizeof *rel);
    c = calloc(s->nvar, sizeof *c);
    lower = malloc(s->nvar * sizeof *lower);
    upper = malloc(s->nvar * sizeof *upper);
    integer = calloc(s->nvar, 1);
    if (!a || !b || !rel || !c || !lower || !upper || !integer)
        goto done;
    /* a probe's function is p0 + coef.(x - x0): its constant part is p0 - coef.x0 */
    for (j = 0; j < s->nvar; j++) {
        double t = coef[j];
        c[j] = model->goal == OC_SOLVER_MIN ? -t : model->goal == OC_SOLVER_MAX ? t : 0.0;
        lower[j] = model->nonnegative ? 0.0 : -HUGE_VAL;
        upper[j] = HUGE_VAL;
    }
    for (k = 0; k < model->nconstraints; k++) {
        const oc_solver_constraint *cn = &model->constraints[k];
        for (i = 0; i < s->con[k].count; i++) {
            unsigned li = s->con[k].lhs + i;
            if (cn->rel == OC_SOLVER_INT || cn->rel == OC_SOLVER_BIN) {
                const cell_at *at = &s->probe[li];
                int v = find_var(s, at->sheet, at->row, at->col);
                integer[v] = 1;
                if (cn->rel == OC_SOLVER_BIN) {
                    a[(size_t)rows * s->nvar + (unsigned)v] = 1.0; rel[rows] = 1; b[rows++] = 0.0;
                    a[(size_t)rows * s->nvar + (unsigned)v] = 1.0; rel[rows] = -1; b[rows++] = 1.0;
                }
                continue;
            }
            {
                double constant = p0[li], *row = &a[(size_t)rows * s->nvar];
                for (j = 0; j < s->nvar; j++) {
                    row[j] = coef[(size_t)li * s->nvar + j];
                    constant -= row[j] * s->x0[j];
                }
                if (s->con[k].rhs_cells) {
                    unsigned ri = s->con[k].rhs + (s->con[k].rhs_one ? 0u : i);
                    double rconst = p0[ri];
                    for (j = 0; j < s->nvar; j++) {
                        double rc_ = coef[(size_t)ri * s->nvar + j];
                        row[j] -= rc_;
                        rconst -= rc_ * s->x0[j];
                    }
                    b[rows] = rconst - constant;
                } else {
                    b[rows] = s->con[k].rhs_value - constant;
                }
                rel[rows] = cn->rel == OC_SOLVER_LE ? -1 : cn->rel == OC_SOLVER_EQ ? 0 : 1;
                rows++;
            }
        }
    }
    if (model->goal == OC_SOLVER_VALUE) {
        double constant = p0[0];
        for (j = 0; j < s->nvar; j++) {
            a[(size_t)rows * s->nvar + j] = coef[j];
            constant -= coef[j] * s->x0[j];
        }
        b[rows] = model->value - constant;
        rel[rows] = 0;
        rows++;
    }
    {
        oc_lp prog;
        prog.n = s->nvar;
        prog.m = rows;
        prog.a = a;
        prog.rel = rel;
        prog.b = b;
        prog.c = c;
        prog.lower = lower;
        prog.upper = upper;
        prog.integer = integer;
        prog.max_nodes = model->max_nodes;
        lp = oc_lp_solve(&prog, x, &objective, &nodes);
    }
    res->nodes = nodes;
    rc = OWF_OK;
    switch (lp) {
    case OC_LP_OPTIMAL: res->status = OC_SOLVED; break;
    case OC_LP_LIMIT: res->status = OC_SOLVED_LIMIT; break;
    case OC_LP_INFEASIBLE:
        res->status = OC_SOLVER_INFEASIBLE;
        why(res, "No values meet every constraint.", NULL);
        goto done;
    case OC_LP_UNBOUNDED:
        res->status = OC_SOLVER_UNBOUNDED;
        why(res, "The target has no limit: add constraints that bound it.", NULL);
        goto done;
    case OC_LP_LIMIT_NONE:
        res->status = OC_SOLVER_NO_ANSWER;
        why(res, "Solver stopped at its limit before it found values that meet every constraint.", NULL);
        goto done;
    case OC_LP_TOO_BIG:
        res->status = OC_SOLVER_ERROR;
        why(res, "The model is too big for Solver.", NULL);
        rc = OWF_ERR_UNSUPPORTED;
        goto done;
    default:
        rc = OWF_ERR_MEMORY;
        goto done;
    }

    /* tidy the answer, and check it on the sheet */
    for (j = 0; j < s->nvar; j++) {
        double v = x[j], w = floor(v + 0.5);
        if (fabs(v - w) <= 1e-9 * (1.0 + fabs(v)))
            v = w;
        if (v == 0.0)
            v = 0.0;                          /* no -0 */
        x[j] = v;
        res->sheet[j] = s->var[j].sheet;
        res->row[j] = s->var[j].row;
        res->col[j] = s->var[j].col;
        res->value[j] = v;
    }
    res->nvariables = s->nvar;
    if ((rc = set_vars(s, x)) != OWF_OK)
        goto done;
    if (!read_probes(s, pt, &bad))
        goto unreadable;
    res->objective = pt[0];
    for (k = 0; k < model->nconstraints; k++) {
        const oc_solver_constraint *cn = &model->constraints[k];
        if (cn->rel > OC_SOLVER_GE)
            continue;
        for (i = 0; i < s->con[k].count; i++) {
            double l = pt[s->con[k].lhs + i], rv;
            double tol;
            rv = s->con[k].rhs_cells ? pt[s->con[k].rhs + (s->con[k].rhs_one ? 0u : i)] : s->con[k].rhs_value;
            tol = 1e-6 * (1.0 + fabs(l) + fabs(rv));
            if ((cn->rel == OC_SOLVER_LE && l > rv + tol) || (cn->rel == OC_SOLVER_GE && l < rv - tol) ||
                (cn->rel == OC_SOLVER_EQ && fabs(l - rv) > tol)) {
                probe_name(s, s->con[k].lhs + i, ref);
                why(res, "The answer does not hold on the sheet at %s: the model may not be linear.", ref);
                res->status = OC_SOLVER_NOT_LINEAR;
                goto done;
            }
        }
    }
    rc = OWF_OK;
    goto done;
unreadable:
    probe_name(s, bad, ref);
    why(res, "%s is not a number with these variables, so Solver cannot use it.", ref);
    res->status = OC_SOLVER_NOT_LINEAR;
    rc = OWF_OK;
done:
    if (s) {
        owf_calc_free(s->calc);
        owf_book_free(s->book);
        free(s->probe);
        free(s);
    }
    free(p0); free(pt); free(coef); free(d); free(x);
    free(a); free(b); free(c); free(rel); free(lower); free(upper); free(integer);
    return rc;
}

/* ---- the model kept in the sheet's names ---- */

/* Names compare without case, as Excel's do (ASCII letters only). */
static int same_prefix(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
        if (x != y)
            return x - y;
        if (!x)
            return 0;
    }
    return 0;
}

static int same_word(const char *a, const char *b)
{
    return same_prefix(a, b, (size_t)-1);
}

static const owf_defname *find_name(const owf_book *book, int sheet, const char *name)
{
    int i;
    for (i = 0; i < book->nnames; i++)
        if (book->names[i].sheet == sheet && book->names[i].name && !same_word(book->names[i].name, name))
            return &book->names[i];
    return NULL;
}

static void copy_ref(char *out, size_t size, const char *formula)
{
    size_t n;
    while (*formula == '=' || *formula == ' ')
        formula++;
    n = strlen(formula);
    if (n >= size)
        n = size - 1;
    memcpy(out, formula, n);
    out[n] = 0;
}

static int name_number(const owf_book *book, int sheet, const char *name, double *v)
{
    const owf_defname *n = find_name(book, sheet, name);
    return n && n->formula && number_text(n->formula, v);
}

void oc_solver_model_init(oc_solver_model *model)
{
    memset(model, 0, sizeof *model);
    model->goal = OC_SOLVER_MAX;
    model->nonnegative = 1;
}

int oc_core_solver_model(const oc_core *core, oc_solver_model *model)
{
    const owf_defname *n;
    double v;
    unsigned k, count;
    char name[24];
    int sheet;
    if (!core || !model)
        return 0;
    oc_solver_model_init(model);
    sheet = core->sheet;
    n = find_name(core->book, sheet, "solver_opt");
    if (!n || !n->formula)
        return 0;
    copy_ref(model->target, sizeof model->target, n->formula);
    if (name_number(core->book, sheet, "solver_typ", &v) && v >= 1.0 && v <= 3.0)
        model->goal = (int)v;
    if (name_number(core->book, sheet, "solver_val", &v))
        model->value = v;
    n = find_name(core->book, sheet, "solver_adj");
    if (n && n->formula)
        copy_ref(model->variables, sizeof model->variables, n->formula);
    if (name_number(core->book, sheet, "solver_neg", &v))
        model->nonnegative = v == 1.0;
    if (name_number(core->book, sheet, "solver_eng", &v))
        model->engine = v == 2.0 ? OC_SOLVER_SIMPLEX : OC_SOLVER_NONLINEAR;   /* 1 GRG Nonlinear, 3 Evolutionary */
    else if (name_number(core->book, sheet, "solver_lin", &v))
        model->engine = v == 1.0 ? OC_SOLVER_SIMPLEX : OC_SOLVER_NONLINEAR;   /* Excel 2007: assume linear */
    count = name_number(core->book, sheet, "solver_num", &v) && v > 0.0 ? (unsigned)v : 0u;
    if (count > OC_SOLVER_CONSTRAINTS)
        count = OC_SOLVER_CONSTRAINTS;
    for (k = 0; k < count; k++) {
        oc_solver_constraint *cn = &model->constraints[model->nconstraints];
        const owf_defname *lhs, *rhs;
        snprintf(name, sizeof name, "solver_lhs%u", k + 1u);
        lhs = find_name(core->book, sheet, name);
        snprintf(name, sizeof name, "solver_rel%u", k + 1u);
        if (!lhs || !lhs->formula || !name_number(core->book, sheet, name, &v) || v < 1.0 || v > 5.0)
            continue;
        cn->rel = (int)v;
        copy_ref(cn->lhs, sizeof cn->lhs, lhs->formula);
        snprintf(name, sizeof name, "solver_rhs%u", k + 1u);
        rhs = find_name(core->book, sheet, name);
        if (cn->rel <= OC_SOLVER_GE) {
            if (!rhs || !rhs->formula)
                continue;
            copy_ref(cn->rhs, sizeof cn->rhs, rhs->formula);
        }
        model->nconstraints++;
    }
    return 1;
}

/* An absolute reference with its sheet, from what the user typed ("b2:c3" -> 'Plan'!$B$2:$C$3). */
static int absolute_ref(const owf_book *book, const char *text, int here, char *out, size_t size)
{
    char first[8], last[8], *p = out;
    const char *name;
    size_t left = size, i;
    int sheet, n;
    owf_range r;
    if (!oc_sheet_range(book, text, here, &sheet, &r) || size < 8)
        return 0;
    name = book->sheets[sheet].name;
    *p++ = '\'';
    left--;
    for (i = 0; name[i]; i++) {
        if (left < 4)
            return 0;
        if (name[i] == '\'') { *p++ = '\''; left--; }
        *p++ = name[i];
        left--;
    }
    *p++ = '\'';
    *p++ = '!';
    left -= 2;
    oc_col_name(r.first_col, first);
    oc_col_name(r.last_col, last);
    if (r.first_row == r.last_row && r.first_col == r.last_col)
        n = snprintf(p, left, "$%s$%u", first, r.first_row + 1u);
    else
        n = snprintf(p, left, "$%s$%u:$%s$%u", first, r.first_row + 1u, last, r.last_row + 1u);
    return n >= 0 && (size_t)n < left;
}

static int put_name(owf_book *book, int sheet, const char *name, const char *formula)
{
    int i;
    for (i = 0; i < book->nnames; i++)
        if (book->names[i].sheet == sheet && book->names[i].name && !same_word(book->names[i].name, name)) {
            char *copy = oc_strdup(formula);
            if (!copy)
                return OWF_ERR_MEMORY;
            owf_free(book->names[i].formula);
            book->names[i].formula = copy;
            book->names[i].hidden = 1;
            return OWF_OK;
        }
    return owf_book_add_name(book, name, sheet, formula, 1) < 0 ? OWF_ERR_MEMORY : OWF_OK;
}

/* Takes away a sheet's solver_lhsN, solver_relN and solver_rhsN past count. */
static void drop_constraints(owf_book *book, int sheet, unsigned count)
{
    int i = 0;
    while (i < book->nnames) {
        const char *nm = book->names[i].name;
        unsigned k;
        char tail;
        if (book->names[i].sheet == sheet && nm && strlen(nm) > 10 && !same_prefix(nm, "solver_", 7) &&
            (!same_prefix(nm + 7, "lhs", 3) || !same_prefix(nm + 7, "rel", 3) ||
             !same_prefix(nm + 7, "rhs", 3)) &&
            sscanf(nm + 10, "%u%c", &k, &tail) == 1 && k > count) {
            owf_free(book->names[i].name);
            owf_free(book->names[i].formula);
            memmove(&book->names[i], &book->names[i + 1], (size_t)(book->nnames - i - 1) * sizeof book->names[0]);
            book->nnames--;
            continue;
        }
        i++;
    }
}

static int save_model(owf_book *book, int sheet, const oc_solver_model *model, char *bad, size_t badsize)
{
    char text[OC_SOLVER_REF * 4], one[OC_SOLVER_REF + 8], name[24], part[OC_SOLVER_REF];
    const char *p;
    unsigned k;
    int rc;
    double v;
#define PUT(n_, f_) do { if ((rc = put_name(book, sheet, (n_), (f_))) != OWF_OK) return rc; } while (0)
    if (!absolute_ref(book, model->target, sheet, text, sizeof text)) {
        snprintf(bad, badsize, "%s", model->target);
        return OWF_ERR_FORMAT;
    }
    PUT("solver_opt", text);
    snprintf(text, sizeof text, "%d", model->goal);
    PUT("solver_typ", text);
    owf_number_text(model->value, text);
    PUT("solver_val", text);
    /* the variables, each range made absolute, joined with commas */
    text[0] = 0;
    for (p = model->variables; *p;) {
        size_t n = 0;
        int quoted = 0;
        while (*p == ' ' || *p == ',' || *p == ';')
            p++;
        if (!*p)
            break;
        while (*p && (quoted || (*p != ',' && *p != ';')) && n + 1 < sizeof part) {
            if (*p == '\'')
                quoted = !quoted;
            part[n++] = *p++;
        }
        part[n] = 0;
        if (!absolute_ref(book, part, sheet, one, sizeof one) || strlen(text) + strlen(one) + 2 > sizeof text) {
            snprintf(bad, badsize, "%s", part);
            return OWF_ERR_FORMAT;
        }
        if (text[0])
            strcat(text, ",");
        strcat(text, one);
    }
    PUT("solver_adj", text);
    snprintf(text, sizeof text, "%u", model->nconstraints);
    PUT("solver_num", text);
    for (k = 0; k < model->nconstraints; k++) {
        const oc_solver_constraint *cn = &model->constraints[k];
        if (!absolute_ref(book, cn->lhs, sheet, text, sizeof text)) {
            snprintf(bad, badsize, "%s", cn->lhs);
            return OWF_ERR_FORMAT;
        }
        snprintf(name, sizeof name, "solver_lhs%u", k + 1u);
        PUT(name, text);
        snprintf(name, sizeof name, "solver_rel%u", k + 1u);
        snprintf(text, sizeof text, "%d", cn->rel);
        PUT(name, text);
        if (cn->rel == OC_SOLVER_INT)
            strcpy(text, "\"integer\"");
        else if (cn->rel == OC_SOLVER_BIN)
            strcpy(text, "\"binary\"");
        else if (number_text(cn->rhs, &v))
            owf_number_text(v, text);
        else if (!absolute_ref(book, cn->rhs, sheet, text, sizeof text)) {
            snprintf(bad, badsize, "%s", cn->rhs);
            return OWF_ERR_FORMAT;
        }
        snprintf(name, sizeof name, "solver_rhs%u", k + 1u);
        PUT(name, text);
    }
    drop_constraints(book, sheet, model->nconstraints);
    PUT("solver_neg", model->nonnegative ? "1" : "2");
    PUT("solver_eng", model->engine == OC_SOLVER_NONLINEAR ? "1" : "2");   /* Excel's GRG Nonlinear, Simplex LP */
    PUT("solver_ver", "3");
#undef PUT
    return OWF_OK;
}

int oc_core_solver_apply(oc_core *core, const oc_solver_model *model, const oc_solver_result *res,
                         char *bad, size_t badsize)
{
    owf_book *work = NULL, *old_book;
    owf_calc *calc = NULL, *old_calc;
    oc_undo *undo;
    unsigned j;
    int rc;
    char text[48], dummy[8];
    if (!bad || !badsize) {
        bad = dummy;
        badsize = sizeof dummy;
    }
    bad[0] = 0;
    if (!core || !model)
        return OWF_ERR_FORMAT;
    if (core->edit_mode)
        oc_edit_commit_anyway(core, OC_MOVE_NONE);
    work = oc_book_copy(core->book);
    if (!work)
        return OWF_ERR_MEMORY;
    calc = owf_calc_new(work);
    if (!calc) {
        owf_book_free(work);
        return OWF_ERR_MEMORY;
    }
    rc = save_model(work, core->sheet, model, bad, badsize);
    for (j = 0; rc == OWF_OK && res && j < res->nvariables; j++) {
        if (res->sheet[j] < 0 || res->sheet[j] >= work->nsheets) {
            rc = OWF_ERR_FORMAT;
            break;
        }
        owf_number_text(res->value[j], text);
        rc = owf_calc_enter(calc, res->sheet[j], res->row[j], res->col[j], text);
    }
    if (rc == OWF_OK)
        rc = owf_calc_recalc(calc);
    if (rc != OWF_OK) {
        owf_calc_free(calc);
        owf_book_free(work);
        return rc;
    }
    undo = oc_undo_new(core, OC_UNDO_BOOK);
    if (undo && oc_undo_save_book(core, undo) != OWF_OK) {
        oc_undo_free(undo);
        undo = NULL;
    }
    oc_cf_forget(core);
    oc_charts_forget(core);
    old_book = core->book;
    old_calc = core->calc;
    core->book = work;
    core->calc = calc;
    owf_calc_free(old_calc);
    owf_book_free(old_book);
    oc_undo_push(core, undo);
    oc_changed(core);
    core->used_valid = 0;
    core->unchecked = 0;
    if (core->clip.valid)
        core->clip.cut = 0;
    return OWF_OK;
}

/*
 * GFOpenCalc's portable core: Goal Seek. The numerical search works on a
 * private workbook so trying values cannot change the open document, its
 * dependency graph or Undo. A front end applies the returned value with
 * oc_core_set_input only after the user accepts it.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "oc_internal.h"

#include <float.h>
#include <math.h>

static int finite_number(double value)
{
    return value == value && value <= DBL_MAX && value >= -DBL_MAX;
}

static int opposite(double a, double b)
{
    return (a < 0.0 && b > 0.0) || (a > 0.0 && b < 0.0);
}

static int goal_value(owf_calc *calc, owf_book *book,
                      int result_sheet, unsigned result_row, unsigned result_col,
                      int variable_sheet, unsigned variable_row, unsigned variable_col,
                      double variable, double target, double *difference, double *value)
{
    const owf_cell *cell;
    char input[48];
    int rc;
    owf_number_text(variable, input);
    rc = owf_calc_enter(calc, variable_sheet, variable_row, variable_col, input);
    if (rc != OWF_OK)
        return rc == OWF_ERR_MEMORY ? rc : OWF_ERR_UNSUPPORTED;
    rc = owf_calc_recalc(calc);
    if (rc != OWF_OK)
        return rc;
    cell = owf_sheet_find(&book->sheets[result_sheet], result_row, result_col);
    if (!cell || cell->type != OWF_VALUE_NUMBER || !finite_number(cell->number))
        return OWF_ERR_UNSUPPORTED;
    *value = cell->number;
    *difference = cell->number - target;
    return finite_number(*difference) ? OWF_OK : OWF_ERR_UNSUPPORTED;
}

int oc_core_goal_seek(const oc_core *core,
                      int result_sheet, unsigned result_row, unsigned result_col,
                      double target,
                      int variable_sheet, unsigned variable_row, unsigned variable_col,
                      double tolerance, int max_iterations,
                      double *solution, double *achieved, int *iterations)
{
    const owf_cell *result_cell, *variable_cell;
    owf_book *copy;
    owf_calc *calc;
    double x0, x1, f0, f1, y, step, tol;
    double lo = 0.0, hi = 0.0, flo = 0.0, fhi = 0.0;
    double best_x, best_y, best_f;
    int bracket = 0, count = 0, rc, i;

    if (!core || !solution || !achieved || !iterations || !finite_number(target) ||
        result_sheet < 0 || result_sheet >= core->book->nsheets ||
        variable_sheet < 0 || variable_sheet >= core->book->nsheets ||
        result_row >= OC_MAX_ROWS || result_col >= OC_MAX_COLS ||
        variable_row >= OC_MAX_ROWS || variable_col >= OC_MAX_COLS ||
        (result_sheet == variable_sheet && result_row == variable_row && result_col == variable_col))
        return OWF_ERR_FORMAT;
    result_cell = owf_sheet_find(&core->book->sheets[result_sheet], result_row, result_col);
    variable_cell = owf_sheet_find(&core->book->sheets[variable_sheet], variable_row, variable_col);
    if (!result_cell || !result_cell->formula ||
        (variable_cell && (variable_cell->formula ||
         (variable_cell->type != OWF_VALUE_EMPTY && variable_cell->type != OWF_VALUE_NUMBER))))
        return OWF_ERR_FORMAT;

    x0 = variable_cell && variable_cell->type == OWF_VALUE_NUMBER ? variable_cell->number : 0.0;
    if (!finite_number(x0))
        return OWF_ERR_FORMAT;
    tol = tolerance > 0.0 ? tolerance : 1e-7 * (fabs(target) > 1.0 ? fabs(target) : 1.0);
    if (!finite_number(tol))
        return OWF_ERR_FORMAT;
    if (max_iterations <= 0)
        max_iterations = 100;
    if (max_iterations > 1000)
        max_iterations = 1000;

    copy = oc_book_copy(core->book);
    if (!copy)
        return OWF_ERR_MEMORY;
    calc = owf_calc_new(copy);
    if (!calc) {
        owf_book_free(copy);
        return OWF_ERR_MEMORY;
    }
    rc = owf_calc_recalc_all(calc);
    if (rc != OWF_OK)
        goto done;
    rc = goal_value(calc, copy, result_sheet, result_row, result_col,
                    variable_sheet, variable_row, variable_col, x0, target, &f0, &y);
    if (rc != OWF_OK)
        goto done;
    best_x = x0;
    best_y = y;
    best_f = fabs(f0);
    if (best_f <= tol) {
        rc = OWF_OK;
        goto found;
    }

    step = fabs(x0) * 0.1;
    if (step < 1.0)
        step = 1.0;
    x1 = x0 + step;
    rc = goal_value(calc, copy, result_sheet, result_row, result_col,
                    variable_sheet, variable_row, variable_col, x1, target, &f1, &y);
    count = 1;
    if (rc != OWF_OK) {
        x1 = x0 - step;
        rc = goal_value(calc, copy, result_sheet, result_row, result_col,
                        variable_sheet, variable_row, variable_col, x1, target, &f1, &y);
        count++;
    }
    if (rc != OWF_OK)
        goto done;
    if (fabs(f1) < best_f) {
        best_x = x1;
        best_y = y;
        best_f = fabs(f1);
    }
    if (fabs(f1) <= tol) {
        rc = OWF_OK;
        goto found;
    }
    if (opposite(f0, f1)) {
        bracket = 1;
        if (x0 < x1) { lo = x0; flo = f0; hi = x1; fhi = f1; }
        else { lo = x1; flo = f1; hi = x0; fhi = f0; }
    }

    for (i = count; i < max_iterations; ++i) {
        double x, f, denom, span, margin;
        if (bracket) {
            span = hi - lo;
            denom = fhi - flo;
            x = fabs(denom) > DBL_EPSILON ? hi - fhi * span / denom : (lo + hi) * 0.5;
            margin = fabs(span) * 1e-6;
            if (!finite_number(x) || x <= lo + margin || x >= hi - margin)
                x = (lo + hi) * 0.5;
        } else {
            denom = f1 - f0;
            x = fabs(denom) > DBL_EPSILON ? x1 - f1 * (x1 - x0) / denom : x1 + step;
            if (!finite_number(x) || fabs(x - x1) < DBL_EPSILON * (fabs(x1) + 1.0) ||
                fabs(x - x1) > 1e12 * (fabs(x1) + 1.0)) {
                step *= 2.0;
                x = x1 + step;
            }
        }
        if (!finite_number(x)) {
            rc = OWF_ERR_UNSUPPORTED;
            break;
        }
        rc = goal_value(calc, copy, result_sheet, result_row, result_col,
                        variable_sheet, variable_row, variable_col, x, target, &f, &y);
        count = i + 1;
        if (rc != OWF_OK) {
            if (!bracket)
                break;
            x = (lo + hi) * 0.5;
            rc = goal_value(calc, copy, result_sheet, result_row, result_col,
                            variable_sheet, variable_row, variable_col, x, target, &f, &y);
            if (rc != OWF_OK)
                break;
        }
        if (fabs(f) < best_f) {
            best_x = x;
            best_y = y;
            best_f = fabs(f);
        }
        if (fabs(f) <= tol) {
            rc = OWF_OK;
            goto found;
        }
        if (bracket) {
            if (opposite(flo, f)) { hi = x; fhi = f; }
            else { lo = x; flo = f; }
        } else if (opposite(f1, f)) {
            bracket = 1;
            if (x1 < x) { lo = x1; flo = f1; hi = x; fhi = f; }
            else { lo = x; flo = f; hi = x1; fhi = f1; }
        }
        x0 = x1;
        f0 = f1;
        x1 = x;
        f1 = f;
    }
    rc = OWF_ERR_UNSUPPORTED;
found:
    *solution = best_x;
    *achieved = best_y;
    *iterations = count;
done:
    owf_calc_free(calc);
    owf_book_free(copy);
    return rc;
}

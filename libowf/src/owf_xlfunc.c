/*
 * libowf: Excel's built-in functions by the numbers its files give them
 * ([MS-XLS] 2.5.198.17, Ftab), and column names. The reader turns a
 * formula's numbers into names with this table; the writer and the
 * spreadsheet's calculation will go the other way.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "owf_internal.h"

#include <stddef.h>

#define MANY 30

static const owf_xl_function functions[] = {
    /* 0 */ { "COUNT", 0, MANY }, { "IF", 2, 3 }, { "ISNA", 1, 1 }, { "ISERROR", 1, 1 },
    /* 4 */ { "SUM", 0, MANY }, { "AVERAGE", 1, MANY }, { "MIN", 1, MANY }, { "MAX", 1, MANY },
    /* 8 */ { "ROW", 0, 1 }, { "COLUMN", 0, 1 }, { "NA", 0, 0 }, { "NPV", 2, MANY },
    /* 12 */ { "STDEV", 1, MANY }, { "DOLLAR", 1, 2 }, { "FIXED", 1, 3 }, { "SIN", 1, 1 },
    /* 16 */ { "COS", 1, 1 }, { "TAN", 1, 1 }, { "ATAN", 1, 1 }, { "PI", 0, 0 },
    /* 20 */ { "SQRT", 1, 1 }, { "EXP", 1, 1 }, { "LN", 1, 1 }, { "LOG10", 1, 1 },
    /* 24 */ { "ABS", 1, 1 }, { "INT", 1, 1 }, { "SIGN", 1, 1 }, { "ROUND", 2, 2 },
    /* 28 */ { "LOOKUP", 2, 3 }, { "INDEX", 2, 4 }, { "REPT", 2, 2 }, { "MID", 3, 3 },
    /* 32 */ { "LEN", 1, 1 }, { "VALUE", 1, 1 }, { "TRUE", 0, 0 }, { "FALSE", 0, 0 },
    /* 36 */ { "AND", 1, MANY }, { "OR", 1, MANY }, { "NOT", 1, 1 }, { "MOD", 2, 2 },
    /* 40 */ { "DCOUNT", 3, 3 }, { "DSUM", 3, 3 }, { "DAVERAGE", 3, 3 }, { "DMIN", 3, 3 },
    /* 44 */ { "DMAX", 3, 3 }, { "DSTDEV", 3, 3 }, { "VAR", 1, MANY }, { "DVAR", 3, 3 },
    /* 48 */ { "TEXT", 2, 2 }, { "LINEST", 1, 4 }, { "TREND", 1, 4 }, { "LOGEST", 1, 4 },
    /* 52 */ { "GROWTH", 1, 4 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 56 */ { "PV", 3, 5 }, { "FV", 3, 5 }, { "NPER", 3, 5 }, { "PMT", 3, 5 },
    /* 60 */ { "RATE", 3, 6 }, { "MIRR", 3, 3 }, { "IRR", 1, 2 }, { "RAND", 0, 0 },
    /* 64 */ { "MATCH", 2, 3 }, { "DATE", 3, 3 }, { "TIME", 3, 3 }, { "DAY", 1, 1 },
    /* 68 */ { "MONTH", 1, 1 }, { "YEAR", 1, 1 }, { "WEEKDAY", 1, 2 }, { "HOUR", 1, 1 },
    /* 72 */ { "MINUTE", 1, 1 }, { "SECOND", 1, 1 }, { "NOW", 0, 0 }, { "AREAS", 1, 1 },
    /* 76 */ { "ROWS", 1, 1 }, { "COLUMNS", 1, 1 }, { "OFFSET", 3, 5 }, { NULL, 0, 0 },
    /* 80 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { "SEARCH", 2, 3 }, { "TRANSPOSE", 1, 1 },
    /* 84 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { "TYPE", 1, 1 }, { NULL, 0, 0 },
    /* 88 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 92 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 96 */ { NULL, 0, 0 }, { "ATAN2", 2, 2 }, { "ASIN", 1, 1 }, { "ACOS", 1, 1 },
    /* 100 */ { "CHOOSE", 2, MANY }, { "HLOOKUP", 3, 4 }, { "VLOOKUP", 3, 4 }, { NULL, 0, 0 },
    /* 104 */ { NULL, 0, 0 }, { "ISREF", 1, 1 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 108 */ { NULL, 0, 0 }, { "LOG", 1, 2 }, { NULL, 0, 0 }, { "CHAR", 1, 1 },
    /* 112 */ { "LOWER", 1, 1 }, { "UPPER", 1, 1 }, { "PROPER", 1, 1 }, { "LEFT", 1, 2 },
    /* 116 */ { "RIGHT", 1, 2 }, { "EXACT", 2, 2 }, { "TRIM", 1, 1 }, { "REPLACE", 4, 4 },
    /* 120 */ { "SUBSTITUTE", 3, 4 }, { "CODE", 1, 1 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 124 */ { "FIND", 2, 3 }, { "CELL", 1, 2 }, { "ISERR", 1, 1 }, { "ISTEXT", 1, 1 },
    /* 128 */ { "ISNUMBER", 1, 1 }, { "ISBLANK", 1, 1 }, { "T", 1, 1 }, { "N", 1, 1 },
    /* 132 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 136 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 140 */ { "DATEVALUE", 1, 1 }, { "TIMEVALUE", 1, 1 }, { "SLN", 3, 3 }, { "SYD", 4, 4 },
    /* 144 */ { "DDB", 4, 5 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 148 */ { "INDIRECT", 1, 2 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 152 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 156 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 160 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { "CLEAN", 1, 1 }, { "MDETERM", 1, 1 },
    /* 164 */ { "MINVERSE", 1, 1 }, { "MMULT", 2, 2 }, { NULL, 0, 0 }, { "IPMT", 4, 6 },
    /* 168 */ { "PPMT", 4, 6 }, { "COUNTA", 0, MANY }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 172 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 176 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 180 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { "PRODUCT", 0, MANY },
    /* 184 */ { "FACT", 1, 1 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 188 */ { NULL, 0, 0 }, { "DPRODUCT", 3, 3 }, { "ISNONTEXT", 1, 1 }, { NULL, 0, 0 },
    /* 192 */ { NULL, 0, 0 }, { "STDEVP", 1, MANY }, { "VARP", 1, MANY }, { "DSTDEVP", 3, 3 },
    /* 196 */ { "DVARP", 3, 3 }, { "TRUNC", 1, 2 }, { "ISLOGICAL", 1, 1 }, { "DCOUNTA", 3, 3 },
    /* 200 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 204 */ { "USDOLLAR", 1, 2 }, { "FINDB", 2, 3 }, { "SEARCHB", 2, 3 }, { "REPLACEB", 4, 4 },
    /* 208 */ { "LEFTB", 1, 2 }, { "RIGHTB", 1, 2 }, { "MIDB", 3, 3 }, { "LENB", 1, 1 },
    /* 212 */ { "ROUNDUP", 2, 2 }, { "ROUNDDOWN", 2, 2 }, { "ASC", 1, 1 }, { "DBCS", 1, 1 },
    /* 216 */ { "RANK", 2, 3 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { "ADDRESS", 2, 5 },
    /* 220 */ { "DAYS360", 2, 3 }, { "TODAY", 0, 0 }, { "VDB", 5, 7 }, { NULL, 0, 0 },
    /* 224 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { "MEDIAN", 1, MANY },
    /* 228 */ { "SUMPRODUCT", 1, MANY }, { "SINH", 1, 1 }, { "COSH", 1, 1 }, { "TANH", 1, 1 },
    /* 232 */ { "ASINH", 1, 1 }, { "ACOSH", 1, 1 }, { "ATANH", 1, 1 }, { "DGET", 3, 3 },
    /* 236 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 240 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 244 */ { "INFO", 1, 1 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { "DB", 4, 5 },
    /* 248 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 252 */ { "FREQUENCY", 2, 2 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 256 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 260 */ { NULL, 0, 0 }, { "ERROR.TYPE", 1, 1 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 264 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 268 */ { NULL, 0, 0 }, { "AVEDEV", 1, MANY }, { "BETADIST", 3, 5 }, { "GAMMALN", 1, 1 },
    /* 272 */ { "BETAINV", 3, 5 }, { "BINOMDIST", 4, 4 }, { "CHIDIST", 2, 2 }, { "CHIINV", 2, 2 },
    /* 276 */ { "COMBIN", 2, 2 }, { "CONFIDENCE", 3, 3 }, { "CRITBINOM", 3, 3 }, { "EVEN", 1, 1 },
    /* 280 */ { "EXPONDIST", 3, 3 }, { "FDIST", 3, 3 }, { "FINV", 3, 3 }, { "FISHER", 1, 1 },
    /* 284 */ { "FISHERINV", 1, 1 }, { "FLOOR", 2, 2 }, { "GAMMADIST", 4, 4 }, { "GAMMAINV", 3, 3 },
    /* 288 */ { "CEILING", 2, 2 }, { "HYPGEOMDIST", 4, 4 }, { "LOGNORMDIST", 3, 3 }, { "LOGINV", 3, 3 },
    /* 292 */ { "NEGBINOMDIST", 3, 3 }, { "NORMDIST", 4, 4 }, { "NORMSDIST", 1, 1 }, { "NORMINV", 3, 3 },
    /* 296 */ { "NORMSINV", 1, 1 }, { "STANDARDIZE", 3, 3 }, { "ODD", 1, 1 }, { "PERMUT", 2, 2 },
    /* 300 */ { "POISSON", 3, 3 }, { "TDIST", 3, 3 }, { "WEIBULL", 4, 4 }, { "SUMXMY2", 2, 2 },
    /* 304 */ { "SUMX2MY2", 2, 2 }, { "SUMX2PY2", 2, 2 }, { "CHITEST", 2, 2 }, { "CORREL", 2, 2 },
    /* 308 */ { "COVAR", 2, 2 }, { "FORECAST", 3, 3 }, { "FTEST", 2, 2 }, { "INTERCEPT", 2, 2 },
    /* 312 */ { "PEARSON", 2, 2 }, { "RSQ", 2, 2 }, { "STEYX", 2, 2 }, { "SLOPE", 2, 2 },
    /* 316 */ { "TTEST", 4, 4 }, { "PROB", 3, 4 }, { "DEVSQ", 1, MANY }, { "GEOMEAN", 1, MANY },
    /* 320 */ { "HARMEAN", 1, MANY }, { "SUMSQ", 0, MANY }, { "KURT", 1, MANY }, { "SKEW", 1, MANY },
    /* 324 */ { "ZTEST", 2, 3 }, { "LARGE", 2, 2 }, { "SMALL", 2, 2 }, { "QUARTILE", 2, 2 },
    /* 328 */ { "PERCENTILE", 2, 2 }, { "PERCENTRANK", 2, 3 }, { "MODE", 1, MANY }, { "TRIMMEAN", 2, 2 },
    /* 332 */ { "TINV", 2, 2 }, { NULL, 0, 0 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 336 */ { "CONCATENATE", 0, MANY }, { "POWER", 2, 2 }, { NULL, 0, 0 }, { NULL, 0, 0 },
    /* 340 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { "RADIANS", 1, 1 }, { "DEGREES", 1, 1 },
    /* 344 */ { "SUBTOTAL", 2, MANY }, { "SUMIF", 2, 3 }, { "COUNTIF", 2, 2 }, { "COUNTBLANK", 1, 1 },
    /* 348 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { "ISPMT", 4, 4 }, { "DATEDIF", 3, 3 },
    /* 352 */ { "DATESTRING", 1, 1 }, { "NUMBERSTRING", 2, 2 }, { "ROMAN", 1, 2 }, { NULL, 0, 0 },
    /* 356 */ { NULL, 0, 0 }, { NULL, 0, 0 }, { "GETPIVOTDATA", 2, MANY }, { "HYPERLINK", 1, 2 },
    /* 360 */ { "PHONETIC", 1, 1 }, { "AVERAGEA", 1, MANY }, { "MAXA", 1, MANY }, { "MINA", 1, MANY },
    /* 364 */ { "STDEVPA", 1, MANY }, { "VARPA", 1, MANY }, { "STDEVA", 1, MANY }, { "VARA", 1, MANY },
    /* 368 */ { "BAHTTEXT", 1, 1 },
};

const owf_xl_function *owf_xl_function_at(unsigned index)
{
    if (index >= sizeof functions / sizeof functions[0] || !functions[index].name)
        return NULL;
    return &functions[index];
}

void owf_column_name(unsigned col, char *out)
{
    char tmp[8];
    int n = 0;
    col++;
    while (col && n < 7) {
        col--;
        tmp[n++] = (char)('A' + col % 26);
        col /= 26;
    }
    while (n)
        *out++ = tmp[--n];
    *out = 0;
}

/* CellValue.rexx: a cell's value and formula in GFOpenCalc.
   rx CellValue.rexx B3   (GFOpenCalc running) */
OPTIONS RESULTS
PARSE UPPER ARG cell
IF cell = '' THEN cell = 'A1'
IF ~SHOW('P', 'GFOPENCALC') THEN DO
    SAY 'GFOpenCalc is not running.'
    EXIT 5
END
ADDRESS 'GFOPENCALC'
'GETCELL' cell
SAY cell '=' RESULT
'GETFORMULA' cell
IF rc = 0 & RESULT ~= '' THEN SAY 'Formula:' RESULT

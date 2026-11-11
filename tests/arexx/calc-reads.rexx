/* tests/arexx/calc-reads.rexx: what GFOpenCalc's window reads of the book (the sheets and their names, cell
   comments, fonts, validation rules, charts, writing a copy), printed so that two builds can be compared line
   for line. rx calc-reads.rexx DIR   (GFOpenCalc running; DIR holds features-openpyxl.xlsx,
   validation-openpyxl.xlsx and charts-openpyxl.xlsx from tests/book/fixtures; prints the answers, then DONE) */
OPTIONS RESULTS
PARSE ARG dir
IF dir = '' THEN dir = ''
ELSE IF RIGHT(dir, 1) ~= ':' & RIGHT(dir, 1) ~= '/' THEN dir = dir'/'
port = 'GFOPENCALC'
IF ~SHOW('P', port) THEN DO
    SAY 'FAIL' port 'is not there: start GFOpenCalc first'
    EXIT 20
END
ADDRESS VALUE port

CALL ask 'OPEN' dir'features-openpyxl.xlsx'
CALL ask 'SHEETS'
n = res
DO i = 1 TO n
    CALL ask 'SHEET' i
    CALL ask 'SHEET'
END
CALL ask 'SHEET 1'
CALL ask 'GETCOMMENT A7'
CALL ask 'GETCOMMENT C9'
CALL ask 'GETCOMMENT F12'
CALL ask 'GETCOMMENT B2'
CALL ask 'FONTCHECK A1'
CALL ask 'FONTCHECK A7'
CALL ask 'FONTCHECK C9'
CALL ask 'VALIDCHECK'
CALL ask 'CHARTCHECK'
CALL ask 'EXPORT RAM:calc-reads.csv'
CALL ask 'ADDSHEET Extra'
CALL ask 'SHEETS'
CALL ask 'RENAMESHEET Renamed'
CALL ask 'SHEET Renamed'
CALL ask 'SHEET'
CALL ask 'SHEET nosuchsheet'
CALL ask 'COMMAND UNDO'
CALL ask 'COMMAND UNDO'
CALL ask 'SHEETS'
CALL ask 'SAVEAS RAM:calc-reads.xlsx'          /* saved: the next OPEN is not held by unsaved changes */

CALL ask 'OPEN' dir'validation-openpyxl.xlsx'
CALL ask 'SHEETS'
CALL ask 'VALIDCHECK'
CALL ask 'FONTCHECK A1'

CALL ask 'OPEN' dir'charts-openpyxl.xlsx'
CALL ask 'SHEETS'
CALL ask 'CHARTCHECK'
CALL ask 'SHEET 2'
CALL ask 'CHARTCHECK'
CALL ask 'SHEET 1'
SAY 'DONE'
EXIT 0

ask: PROCEDURE EXPOSE res
    PARSE ARG cmd
    res = ''
    cmd
    got = rc
    IF got = 0 THEN res = result
    SAY cmd '=>' got '|' res
    RETURN

/* WordCount.rexx: the words and pages of the document in GFOpenWrite.
   rx WordCount.rexx   (GFOpenWrite running) */
OPTIONS RESULTS
IF ~SHOW('P', 'GFOPENWRITE') THEN DO
    SAY 'GFOpenWrite is not running.'
    EXIT 5
END
ADDRESS 'GFOPENWRITE'
'VERSION'
SAY RESULT
'WORDCOUNT'
words = RESULT
'PAGECOUNT'
SAY words 'words on' RESULT 'pages'

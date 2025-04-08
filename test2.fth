: TRIPLE DUP DUP + + ;
: POW2 DUP 0 = IF DROP DROP 1 . EXIT THEN DUP 1 = IF DROP . EXIT THEN 1 PICK SWAP 1 - 0 DO 1 PICK * LOOP SWAP DROP . ;
: TEST-CASE   CASE  1 OF  ." UN " CR  ENDOF 2 OF  ." DEUX " CR ENDOF 3 OF  ." TROIS "  CR ENDOF ." Others " CR  ENDCASE ;
: FACTLOOP 1 + DUP 0 = IF DROP 1 EXIT THEN DUP 1 = IF DROP 1 EXIT THEN 1 SWAP 1 DO I * LOOP ;
: HELLO  ." Hello " USERNAME @ PRINT ."  How are uou ? " CR ;
: MACRON  ." Macron est un saint homme ! " CR ;
: TEST-DELAY  COUNT ! BEGIN ." Hello " CR 1000 DELAY COUNT @ 1 - DUP COUNT ! 0 = UNTIL ;
: CREDIT ." Brought  to you by Cleobuline https://github.com/cleobuline/forth-bot-gmp-irc-threaded-multi-users/tree/main Site https://labynet.fr " CR  ;
VARIABLE START-TIME
: SET-START MICRO START-TIME ! ;
: TIME-SINCE MICRO START-TIME @ - ;
: SHOW-TIME TIME-SINCE . ;

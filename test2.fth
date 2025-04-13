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

 

: TABLE-MULT ( n -- )
  DUP CR 1 + 1 DO
    DUP 1 + 1 DO
      I J * DUP DUP 100 < IF 32 EMIT THEN DUP 10 < IF 32 EMIT THEN NUM-TO-STR 32 EMIT DROP
    LOOP
    124 EMIT CR
  LOOP DROP ;
  : TABLE DUP CR 1 + 1 DO DUP 1 + 1 DO I J * NUM-TO-STR 32 EMIT LOOP 124 EMIT CR LOOP DROP ;
  
 
 
  
: LEAP-YEAR? DUP 4 MOD 0 = IF DUP 100 MOD 0 = IF 400 MOD 0 = EXIT THEN DROP 1 EXIT THEN DROP 0 ;

: MONTH-NAME ( n -- )
  DUP 1 = IF   ." January" EXIT THEN
  DUP 2 = IF   ." February" EXIT THEN
  DUP 3 = IF   ." March" EXIT THEN
  DUP 4 = IF   ." April" EXIT THEN
  DUP 5 = IF   ." May" EXIT THEN
  DUP 6 = IF   ." June" EXIT THEN
  DUP 7 = IF   ." July" EXIT THEN
  DUP 8 = IF   ." August" EXIT THEN
  DUP 9 = IF   ." September" EXIT THEN
  DUP 10 = IF   ." October" EXIT THEN
  DUP 11 = IF   ." November" EXIT THEN
  DUP 12 = IF   ." December" EXIT THEN
   DROP  ." Unknown" ;

: DAYS-IN-MONTH ( month year -- days )
  OVER 2 = IF LEAP-YEAR? IF 29 ELSE 28 THEN EXIT THEN
  OVER 4 = IF DROP 30 EXIT THEN
  OVER 6 = IF DROP 30 EXIT THEN
  OVER 9 = IF DROP 30 EXIT THEN
  OVER 11 = IF DROP 30 EXIT THEN
  DROP DROP 31 ;
  
: ZELLER ( month year -- day )
  >R
  DUP 3 < IF
    12 + R> 1 - >R
  THEN
  R@ 100 MOD
  R@ 100 /
  SWAP
  DUP 4 / +
  SWAP
  2 * 0 SWAP - +
  SWAP
  1 + 13 * 5 /
  1 +
  +
  R> 100 / 4 / +
  6 + ( Décalage de -1 jour )
  7 MOD
 
;
: 2SWAP ( x1 x2 x3 x4 -- x3 x4 x1 x2 )
  ROT >R ROT R> ;
: SPACES ( n -- )
  0 DO 32 EMIT LOOP ;
  
: SPACES ( N -- ) 0 DO 32 EMIT LOOP ;

 
 

: NIP ( a b -- b )
  SWAP DROP ;
  
 : MAX ( n1 n2 -- n )
  2DUP > IF DROP ELSE SWAP DROP THEN ;


  VARIABLE OFF

: CAL ( month year -- )
  2DUP
  SWAP MONTH-NAME DROP
  32 EMIT NUM-TO-STR CR
  ." Mo Tu We Th Fr Sa Su" CR

  2DUP ZELLER   
  DUP 0 = IF DROP 6 ELSE 1 - THEN DUP OFF !
   

  OFF @ 0 > IF   
    OFF @ 0 DO 32 EMIT 32 EMIT  32 EMIT  LOOP
  THEN

  DROP 2DUP 
  2DUP DAYS-IN-MONTH 1 +  
  1 DO
    I 10 < IF 32 EMIT 32 EMIT ELSE 32 EMIT THEN
    I NUM-TO-STR
    OFF @ I + 7 MOD 0 = IF CR THEN
  LOOP

  2DROP CR ;

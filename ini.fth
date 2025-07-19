: 2SWAP ( x1 x2 x3 x4 -- x3 x4 x1 x2 ) ROT >R ROT R> ;
 : 2DUP OVER OVER ;
 : 3DUP   2 PICK 2 PICK 2  PICK ; 
 
 
: /MOD ( n1 n2 -- rem quot )
  2DUP / >R  MOD R>  ; 
: SPACES ( n -- ) 0 DO 32 EMIT LOOP ;
 
 
 : NUM-TO-STR DUP 10 < IF 48 + EMIT ELSE DUP 10 / RECURSE 10 MOD 48 + EMIT THEN ; 
 : PRINT-ASCII 126 32 1 + DO I NUM-TO-STR 32 EMIT I EMIT 44 EMIT LOOP CR ; 
 : MULTI-CHAR 0 DO DUP EMIT LOOP CR DROP ;
: NIP ( a b -- b ) SWAP DROP ;
 
 : MAX ( n1 n2 -- n ) 2DUP > IF DROP ELSE SWAP DROP THEN ;
 : MIN ( n1 n2 -- n ) 2DUP < IF DROP ELSE SWAP DROP THEN ;
 
: >= ( A B -- FLAG ) < NOT ;
: 0> ( n -- flag ) 0 >  ;
: <= ( A B -- FLAG ) SWAP < NOT ;
: 0=  0 = ; 

: DOUBLE DUP + ;
: FACT DUP 1 > IF DUP 1 - RECURSE * ELSE DROP 1 THEN ; 
: POW DUP 0 = IF DROP 1 EXIT THEN 1 SWAP 0 DO OVER * LOOP SWAP DROP ;
: FIBONACCI DUP 0 = IF DROP 0 EXIT THEN DUP 1 = IF DROP 1 EXIT THEN DUP 2 = IF DROP 1 EXIT THEN 1 1 ROT 2 - 0 DO OVER + SWAP LOOP DROP ;
: COUNTDOWN 10 BEGIN DUP . 1 - DUP 0 > WHILE REPEAT DROP ;
: TUCK SWAP OVER ;

: REVERSE DUP 2 < IF DROP EXIT THEN 1 DO I ROLL LOOP ;
 
 
: SQUARE-SUM ( x1 x2 ... xn n -- sum )
  0 >R          
  0 DO          
    DUP *      
    R@UL +      
    R!UL     
  LOOP
  R> ;         
 : SQUARE-STACK DEPTH SQUARE-SUM ; 
 : REVERSE-STACK DEPTH REVERSE ;
 : CARRE DUP * ;
: SUM-SQUARED 0 SWAP 1 + 0 DO I + LOOP CARRE ;


 
: RECUNACCI 2 + 0 DO I FIBONACCI LOOP DROP ; 
: CAT 32 EMIT 32 EMIT 32 EMIT 47 EMIT 95 EMIT
 47 EMIT 32 EMIT CR 40 EMIT 32 EMIT 111 EMIT 46 EMIT 
 111 EMIT 32 EMIT 41 EMIT CR 32 EMIT 32 EMIT
  62 EMIT 32 EMIT 94 EMIT 32 EMIT 60 EMIT CR ;
  
: :D ." :D" CR  ;
: PGCD DUP 0 = IF DROP ELSE SWAP OVER MOD RECURSE THEN ;
: FPRIME? DUP 2 < IF DROP 0 EXIT THEN DUP 2 = IF DROP 1 EXIT THEN DUP 2 MOD 0 = IF DROP 0 EXIT THEN DUP SQRT 3 DO DUP I MOD 0 = IF DROP 0 UNLOOP EXIT THEN 2 +LOOP DROP 1 ;
 
: FPRIME2? 
 DUP 2 < IF DROP 0 EXIT THEN ( n < 2 → non premier )
 DUP 2 = IF DROP 1 EXIT THEN ( n = 2 → premier )
 DUP 3 = IF DROP 1 EXIT THEN ( n = 3 → premier )
 DUP 5 = IF DROP 1 EXIT THEN ( n = 5 → premier )
 DUP 2 MOD 0 = IF DROP 0 EXIT THEN ( Pair → non premier )
 DUP 3 MOD 0 = IF DROP 0 EXIT THEN ( Multiple de 3 → non premier )
 DUP 5 MOD 0 = IF DROP 0 EXIT THEN ( Multiple de 5 → non premier )
 DUP SQRT 5 DO ( Boucle commence à 5 )
 DUP I MOD 0 = IF DROP 0 UNLOOP EXIT THEN ( Teste i )
 DUP I 2 + MOD 0 = IF DROP 0 UNLOOP EXIT THEN ( Teste i + 2 )
 6 +LOOP ( Pas de 6 6k ± 1 )
 DROP 1 ; ( Sinon → premier )
 
 
VARIABLE SEED
VARIABLE COUNT
CREATE NUMS 
NUMS 51 ALLOT
CREATE STARS 
STARS 13 ALLOT 
: INIT-RANDOM CLOCK SEED ! ;
INIT-RANDOM 
: RANDOM SEED @ 1103515245 * 12345 + DUP SEED ! 2147483648 MOD ;
: RAND OVER - 1 + RANDOM OVER MOD SWAP DROP + ;
: INIT-NUMS 50 0 DO I 1 + I NUMS ! LOOP 0 COUNT ! ;
: SHUFFLE-NUMS 49 1 DO 0 I RAND DUP NUMS @ I NUMS @ SWAP I NUMS ! OVER NUMS ! DROP LOOP ;
: PICK-NUM COUNT @ DUP 50 < IF NUMS @ COUNT @ 1 + COUNT ! ELSE DROP 1 THEN ;
 
: INIT-STARS 12 0 DO I 1 + I STARS ! LOOP 0 COUNT ! ;
: SHUFFLE-STARS 12 1 DO 0 I RAND DUP STARS @ I STARS @ SWAP I STARS ! OVER STARS ! DROP LOOP ;
: PICK-STAR COUNT @ DUP 12 < IF STARS @ COUNT @ 1 + COUNT ! ELSE DROP 1 THEN ;
: EURO INIT-RANDOM   INIT-NUMS 
  SHUFFLE-NUMS 5 0 DO PICK-NUM   NUM-TO-STR 32 EMIT
   LOOP INIT-STARS SHUFFLE-STARS PICK-STAR   NUM-TO-STR 32 EMIT
   PICK-STAR   NUM-TO-STR       CR ;
 : DIE 1 6 RAND ; 
 : ROLLDICE DIE DIE DIE .S CLEAR-STACK ; 
 
: TRIPLE DUP DUP + + ;
: POW2 DUP 0 = IF DROP DROP 1 . EXIT THEN DUP 1 = IF DROP . EXIT THEN 1 PICK SWAP 1 - 0 DO 1 PICK * LOOP SWAP DROP . ;
: TEST-CASE CASE 1 OF ." UN " CR ENDOF 
2 OF ." DEUX " CR ENDOF 
3 OF ." TROIS " CR ENDOF 
." Others " CR ENDCASE ;

: FACTLOOP 1 + DUP 0 = IF DROP 1 EXIT THEN DUP 1 = IF DROP 1 EXIT THEN 1 SWAP 1 DO I * LOOP ;
: HELLO ." Hello " USERNAME @ PRINT ."  How are you ? " CR ;
: MACRON ." Macron est un saint homme ! " CR ;
  : TEST-DELAY ( n -- ) COUNT ! BEGIN ." Hello " CR 1000 DELAY COUNT @ 1 - DUP COUNT ! 0 = UNTIL ;
: CREDIT ." Brought to you by Cleobuline updated with hashtable  https://github.com/cleobuline/forth-bot-gmp-irc-threaded-multi-users/tree/main Site https://labynet.fr " CR ;
VARIABLE START-TIME
: SET-START MICRO START-TIME ! ;
: TIME-SINCE MICRO START-TIME @ - ;
: SHOW-TIME TIME-SINCE . ;
 
 : PRIME-FACTORS ( n -- p1 p2 ... )
 DUP 1 = IF DROP EXIT THEN
 >R 2
 BEGIN
 R@ 1 >
 WHILE
 DUP DUP * R@ > IF
 R@ NUM-TO-STR 32 EMIT
 R> DROP 1 >R
 ELSE
 R@ OVER MOD 0 = IF
 DUP NUM-TO-STR 32 EMIT
 R@ OVER / R> DROP >R
 ELSE
 1 +
 THEN
 THEN
 REPEAT
 DROP R> DROP CR ;
 
 : SHOW-PRIMES 0 DO I PRIME? IF I NUM-TO-STR 32 EMIT THEN LOOP CR ; 
 
 
 
: TABLE-MULT ( n -- )
 DUP CR 1 + 1 DO
 DUP 1 + 1 DO
 I J * DUP DUP 100 < IF 32 EMIT THEN DUP 10 < IF 32 EMIT THEN NUM-TO-STR 32 EMIT DROP
 LOOP
 124 EMIT CR
 LOOP DROP ;
 : TABLE DUP CR 1 + 1 DO DUP 1 + 1 DO I J * DUP 100 < IF 32 EMIT THEN DUP 10 < IF 32 EMIT THEN NUM-TO-STR 32 EMIT LOOP 124 EMIT CR CR LOOP DROP ;
 
 
 VARIABLE OFF ( offset pour les lignes du calendrier ) 
 VARIABLE DAYS 
VARIABLE YEAR 
VARIABLE MONTH 
VARIABLE DAY
: LEAP-YEAR? DUP 4 MOD 0 = IF DUP 100 MOD 0 = IF 400 MOD 0 = EXIT THEN DROP 1 EXIT THEN DROP 0 ;
 
 
 : MONTH-NAME CASE 1 OF ." January"   ENDOF 
2 OF ." February"   ENDOF 
3 OF ." March"    ENDOF 
4 OF ." April"  ENDOF 
5 OF ." May"  ENDOF 
6 OF ." June"  ENDOF 
7 OF ." July"  ENDOF 
8 OF ." August" ENDOF 
9 OF ." September"   ENDOF 
10 OF ." October"  ENDOF 
11 OF  ." November" ENDOF 
12 OF ." December" ENDOF 
." Others "   ENDCASE ;

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
 
: DAYS-IN-MONTH ( month year -- days )
 SWAP
 DUP 2 = IF DROP LEAP-YEAR? IF 29 ELSE 28 THEN EXIT THEN
 DUP 4 = IF DROP 30 EXIT THEN
 DUP 6 = IF DROP 30 EXIT THEN
 DUP 9 = IF DROP 30 EXIT THEN
 DUP 11 = IF DROP 30 EXIT THEN
 DROP 31 ;
 
: CAL ( day month year -- )
 2DUP
 SWAP MONTH-NAME DROP
 32 EMIT NUM-TO-STR CR
 ." Mo Tu We Th Fr Sa Su" CR
 2DUP ZELLER 
 DUP 0 = IF DROP 6 ELSE 1 - THEN DUP OFF !
 
 OFF @ 0 > IF 
 OFF @ 0 DO 32 EMIT 32 EMIT 32 EMIT LOOP
 THEN
 DROP 2DUP 
 2DUP DAYS-IN-MONTH 1 + 
 1 DO
 I 10 < IF 32 EMIT 32 EMIT ELSE 32 EMIT THEN
 I NUM-TO-STR
 OFF @ I + 7 MOD 0 = IF CR THEN
 LOOP
 2DROP 2DROP  DROP  CR ;
 
 
: DAYS-IN-MONTH ( month year -- days )
 SWAP
 DUP 2 = IF DROP LEAP-YEAR? IF 29 ELSE 28 THEN EXIT THEN
 DUP 4 = IF DROP 30 EXIT THEN
 DUP 6 = IF DROP 30 EXIT THEN
 DUP 9 = IF DROP 30 EXIT THEN
 DUP 11 = IF DROP 30 EXIT THEN
 DROP 31 ;
: CALC-MONTHS
 1 MONTH !
 BEGIN
 DAYS @ 0> WHILE
 MONTH @ YEAR @ DAYS-IN-MONTH
 DUP DAYS @ <= IF
 DAYS @ SWAP - DAYS !
 MONTH @ 1 + MONTH !
 ELSE
 DROP EXIT
 THEN
 REPEAT ;
 
: DAYS-IN-YEAR ( YEAR -- N )
 DUP LEAP-YEAR? IF 366 ELSE 365 THEN ;
 VARIABLE TODAY-DAY
: CALC-YEARS
 1970 YEAR !
 BEGIN
 DAYS @ 0> WHILE
 YEAR @ LEAP-YEAR? IF 366 ELSE 365 THEN
 DUP DAYS @ >= NOT IF
 DAYS @ SWAP - DAYS !
 YEAR @ 1 + YEAR !
 ELSE
 DROP EXIT
 THEN
 REPEAT DROP ;
 
 
 : CALC-DAY DAYS @ 1 + DAY ! ;
 
: TODAY  7200000   MILLI + 1000 / 86400 / DAYS ! 
CALC-YEARS CALC-MONTHS CALC-DAY
 2DROP 2DROP DROP 
 DAY @ MONTH @ YEAR @ ;
 
 
: CAL-IRC-HIGHLIGHT 
 2 PICK TODAY-DAY ! 2DUP SWAP MONTH-NAME DROP 32 EMIT NUM-TO-STR CR
 ." Mo Tu We Th Fr Sa Su" CR
 2DUP ZELLER DUP 0 = IF DROP 6 ELSE 1 - THEN DUP OFF !
 OFF @ 0 > IF OFF @ 0 DO 32 EMIT 32 EMIT 32 EMIT LOOP THEN
 DROP 2DUP 2DUP DAYS-IN-MONTH 1 + 1 DO
 I 10 < IF 32 EMIT 32 EMIT ELSE 32 EMIT THEN
 I TODAY-DAY @ = IF 22 EMIT I NUM-TO-STR 15 EMIT ELSE I NUM-TO-STR THEN
 OFF @ I + 7 MOD 0 = IF CR THEN
 LOOP
 2DROP 2DROP 2DROP    CR ;
 
: TODAY-CAL-IRC TODAY CAL-IRC-HIGHLIGHT ;

: CUBE DUP DUP * * ;
: SUM-CUBES 0 SWAP 1 + 0 DO I CUBE + LOOP ; 
 : 2OVER ( x1 x2 x3 x4 -- x1 x2 x3 x4 x1 x2 )
  2 PICK 2 PICK ;
 
: WEEKDAY-NAME ( n -- )
  DUP 0 = IF ." Mon" EXIT THEN
  DUP 1 = IF ." Tue" EXIT THEN
  DUP 2 = IF ." Wed" EXIT THEN
  DUP 3 = IF ." Thu" EXIT THEN
  DUP 4 = IF ." Fri" EXIT THEN
  DUP 5 = IF ." Sat" EXIT THEN
  DUP 6 = IF ." Sun" EXIT THEN
  DROP ." Unknown" ;
  
: PAD-2DIGIT ( n -- )
  DUP 10 < IF 48 EMIT THEN NUM-TO-STR ;

: PRINT-TIME ( -- )
  MILLI 1000 / 7200 + ( add 2 hours for CEST )
  86400 MOD ( seconds since midnight )
  DUP 3600 / PAD-2DIGIT ( hours )
  58 EMIT ( emit ':' )
  DUP 3600 MOD 60 / PAD-2DIGIT ( minutes )
  58 EMIT ( emit ':' )
  60 MOD PAD-2DIGIT ( seconds ) ;

: GET-DOW ( day month year -- n )
  2DUP ZELLER       ( day month year ZELLER )
  ROT DROP        ( day year ZELLER )
  ROT               ( year ZELLER day )
  1 -              ( year ZELLER day-1 )
  + 7 MOD   SWAP         ( year dow )
  DROP            ( dow )
  DUP 0 = IF DROP 6 ELSE 1 - THEN ;

: DATE ( -- )
  TODAY   ( day month year )
  GET-DOW WEEKDAY-NAME 32 EMIT ( use month year for ZELLER )
  TODAY 
  1 PICK   MONTH-NAME 32 EMIT ( print month )
  DAY @ DUP 10 < IF 32 EMIT THEN NUM-TO-STR 32 EMIT ( print day, space-padded )
  PRINT-TIME 32 EMIT ( print time HH:MM:SS )
  ." CEST" 32 EMIT ( print timezone )
  YEAR @ NUM-TO-STR CR 2DROP 2DROP  DROP  ;
  
 
 
: MOON-PHASE-NAME ( phase -- )
  DUP 0 = IF ." Nouvelle lune" DROP EXIT THEN
  DUP 1 = IF ." Croissant montant" DROP EXIT THEN
  DUP 2 = IF ." Premier quartier" DROP EXIT THEN
  DUP 3 = IF ." Gibbeuse croissante" DROP EXIT THEN
  DUP 4 = IF ." Pleine lune" DROP EXIT THEN
  DUP 5 = IF ." Gibbeuse decroissante" DROP EXIT THEN
  DUP 6 = IF ." Dernier quartier" DROP EXIT THEN
  ." Croissant descendant" DROP ;
  
 
: MOON-ICON ( phase -- )
  DUP 0 = IF ." 🌑 " DROP EXIT THEN
  DUP 1 = IF ." 🌒 " DROP EXIT THEN
  DUP 2 = IF ." 🌓 " DROP EXIT THEN
  DUP 3 = IF ." 🌔 " DROP EXIT THEN
  DUP 4 = IF ." 🌕 " DROP EXIT THEN
  DUP 5 = IF ." 🌖 " DROP EXIT THEN
  DUP 6 = IF ." 🌗 " DROP EXIT THEN
  ." 🌘 " DROP ;

: MOON-MESSAGE ( phase -- )
  DUP 0 = IF ." La lune se cache, parfait pour observer les etoiles !" DROP EXIT THEN
  DUP 1 = IF ." Une jeune lune, un nouveau depart a Paris !" DROP EXIT THEN
  DUP 2 = IF ." La lune guide vos soirees !" DROP EXIT THEN
  DUP 3 = IF ." La lune grossit, revez grand ce soir !" DROP EXIT THEN
  DUP 4 = IF ." Admirez-la sur les toits de Paris !" DROP EXIT THEN
  DUP 5 = IF ." La lune decroit, une nuit douce vous attend !" DROP EXIT THEN
  DUP 6 = IF ." Profitez de la serenite !" DROP EXIT THEN
  ." La lune s'efface, une nuit calme a venir !" DROP ;

 
 
: MOON ( -- )
  TODAY
  3DUP
  ." Phase de la lune pour " 
  GET-DOW WEEKDAY-NAME  32 EMIT
  DROP
  1 PICK MONTH-NAME  32 EMIT
  DAY @ DUP 10 < IF 32 EMIT THEN NUM-TO-STR  32 EMIT
  YEAR @ NUM-TO-STR CR
     MOON-PHASE 
 SWAP 
 DUP DUP MOON-ICON  MOON-PHASE-NAME 32 EMIT 
  MOON-MESSAGE  32 EMIT 
  ." Illumination "  NUM-TO-STR ." %"  CR 
  DROP         
;
: TREC ( n -- ) DUP 0 <= IF EXIT THEN DUP . 1 - RECURSE ;

: QUOTE DUP  1 = IF  ." When we are dead we do not know that we are dead. It is for others that it is difficult. When we are jerk it is the same - Jean Claude Vandamme - " CR
ELSE THEN DUP  2 = IF ." Every second laugh is a little time saved on existence -Raphaël Quenard-" CR ELSE THEN 
3 = IF ." The difference between a genius and an idiot? The genius knows he is an idiot, but the idiot does not care. - Anonymous -" CR ELSE THEN ;

." File ini.fth with moon loaded "  

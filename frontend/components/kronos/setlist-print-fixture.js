import { SET_BREAK_NAME, gigMarkerName } from "./setlist-markers.js";

// Real Set List "emergency exit" (Set List 001) from the project owner's own backup
// K1_20260418.PCG -- every used slot's name + comment, decoded by PcgFile (SDB1/SBK1)
// via a throwaway clang++ dump tool, empty slots left out. Real gig data, so the
// print layout gets tested against real comment lengths, real \r\n line breaks and
// the real runs of spaces the comments use for alignment on the Kronos screen.
export const EMERGENCY_EXIT_SETLIST = {
  name: "emergency exit",
  slots: [
    { index: 0, name: "Rolling in the Deep", comment: "Intro (1): [a, e, g, e g]...\r\nBridge (1): [f, g, e, f, g, e\\e-f-h, #g\\e-#g-h-d]\r\nRefr (2): am G F                                   [pad__\\C5/__synth]\r\nStr (1): wie Intro\r\nBridge (1)\r\nRefr (2)\r\na capella, Klatschen, {a}\r\nRefr (2)" },
    { index: 1, name: "I don't wanna miss a Thi", comment: "Intro (1): [e-#f-h]  oben bolero h, #c, e  rechts Tonleiter\r\n                 single note: a__   Ende: d__\r\nStr (2): D   A/#c   bm7   bm7 | G   D/#f   em7\r\n             #fm   G   D   A\r\nRef (3): D   A/#c   em7 | em7   G   A |:2\r\n              D   A  em\r\nStr (2): wie Str1 | 1/2 Str1 | A D A\r\nBridge (4): C+c, G+h, #A, F+a | C G dm A\r\n8 * Ref (bm / D im Wechsel)" },
    { index: 2, name: "Sex on Fire", comment: "Intro (1): E #cm |:n // A\r\nStr (2): Bass 1/8: e, #c (oben single notes)\r\nRefr (2): E #cm A\r\n....\r\nBreak (3): (stac) E #cm A | Bass: a, #g, #f, e, #c, h, a\r\nRefr (2)" },
    { index: 3, name: "Keep the Faith", comment: "Intro (1): g//G2, f//F2\r\nStr A (2): G #A C | #A, G \r\nStr B (2): G #A C | #D, F\r\nRefr (2): G {d-g-c}, #A, F, C |:2 // 'keep the faith...'\r\nStr A (2) // Str A, Str B |:2\r\nRef (2) |:3 // F                            Lauf: f, e, #d, d, #c, c\r\nSolo (2): Str A |:3 // Str B\r\nSprechteil (3): {g f d f g} |:4\r\nStr A (2), Str B // Ref (3): |:2 // Str B (2) |:6" },
    { index: 4, name: "Separate Ways", comment: "Intro (1): LH: e,g \r\nStr (1): em  D  C\r\nBridge (1): am, D | G  D | em  C |:2  //  D, em\r\nRefr (2): em  D  C | am  D, hm |:2\r\nGit (3)\r\n" },
    { index: 5, name: "Call Me", comment: "Intro (1): Lead |:4\r\nStr (1): #cm  A  #cm  A | #fm  #G  #fm  #G\r\nRefr (2): #cm  E  #fm (h-#c-#f-a)  A |:2\r\nIntro // Str // Refr: |:2\r\nBridge (2): #cm  #dm\r\n                   #dm  #am |:2 // E  H | #cm  A #fm  #G\r\nKey Solo (3)\r\nRefr: |:3 // letzter Ref: #cm E #fm #G\r\nIntro: |:" },
    { index: 6, name: "Weak", comment: "Str (1): em   D   C\r\nRefr (1): ...\r\nStr (2): wie 1, rechts Bells\r\nBridge (3): G am(+) C D(+)\r\nRefr (1)" },
    { index: 7, name: "Baby Love", comment: "Intro [1]:    gm\r\n                                   [bass__\\f3/__hapsi__\\#a6/__brass]\r\nStr [2]:       f5   g5 ... | #D, F\r\nRefr [2]:     gm   c   f\r\nBridge [3]:                                          [lead__\\#a6/__bells]\r\nEnde [4]:    [c-g-c]  [bh-f-bh]  \r\n" },
    { index: 8, name: "One Vision", comment: "Intro (1): D G am7 G C | #A C | G2 D\r\nStr (2): D A  | C G | am, A, D |:2\r\nRefr (3): A, D |:2 // C\r\n{a, #a, h, c}\r\nStr (2): // Ref (3)\r\nEnde: {a, #a, h, c} | {a, #a, h, c, #d, d} | #D\r\r" },
    { index: 9, name: "Tie your Mother down", comment: "Str (1): A || G/g, hm/#f, C/c, G/h\r\nBridge (1): E | G5, A5 | {a, h, c, #c}\r\nRefr (1): D ... A | G, D |:2\r\nEnde: D A" },
    { index: 10, name: "Feels like making Love", comment: "Str (1): #F H\r\nRefr (1): e, #f |:2 // H\r\nStr (2): {#f, h #c | h, #c, e....}" },
    { index: 11, name: "Nutbush", comment: "Str (1): arp {a, h, c}\r\nRefr (1): C G\r\nSolo (2): {a...}  [switch left]" },
    { index: 12, name: "Cry Baby", comment: "Refr: C hm D | C #D em F #F G\r\nSolo: G #D F G |:2  ||  C hm D |:2 ||\r\n          C #D em F #F G\r\nBridge: am F C G |:2 ||\r\n              em F hm C | #D F |:3 || G\r\nRefr: C hm D |:4 || C #D G em #D F G\r\nEnde: #D F G |:2" },
    { index: 13, name: "One", comment: "Intro [1]: single note { d,e,h...}\r\nBridge: C, am, Fmaj7, C    |:2\r\nStr [2]: am, D/#f, F, G      | 1*leer, 2*Gesang\r\nBridge [3]: // Str: one love, one... // Str\r\nBrige (mit Basslauf) // C am |:2 // C G6 F\r\nStr: one love, one...\r\nBridge: |:3\r\nStr: |:2    Ende C" },
    { index: 14, name: "On Fire", comment: "Str (1): (F Dur, em7)   g, #a, f, c // F, d-e-g\r\nRefr (2): gm, #A, F, C\r\n" },
    { index: 15, name: "Sober", comment: "Str (1):                                                             -1\r\nRef (1): #dm, H, #F, #F #gm, #dm\r\nBridge (2): H, #F, #dm, #C | H, #F ..Lauf ab #g\r\n...\r\nEnde (3) ohne  (1)" },
    { index: 16, name: "Lost in Space", comment: "                                                          [pad______\\c5/_bells]\r\nIntro (1): a,g,h | a,g, d\r\n                                                          [pad___\\c5/___string]\r\nRefr A (2): dm #A F | #A C dm (C) | #A gm dm | #A C dm\r\nStr (2): A, G, hm | A, G, D |:2\r\nRefr A\r\nBridge (3): gm, F | :2  ||  #A .. Lauf ab #a\r\nRefr B (2): em C G | C D em (D) | C am em | C D" },
    { index: 17, name: "Runaway", comment: "Intro: am..                                 [strings__\\c4/__fastsynth]\r\nStr:\r\nBridge: em, G, am, dm, C, G\r\nSolo: am, C, D | am, C, G" },
    { index: 18, name: "Bring me some Water", comment: "Str (1): am, D    Ende E\r\nRefr (2): A, G, D (, E)  eine Oktave hoch\r\nBridge (2): #fm, G, A | :3  ||  hm, D, E" },
    { index: 19, name: "Like the Way I do", comment: "Str (1): #A, C... | Arp A...\r\nRefr (2): dm, (C) #A, C | | dm...\r\n                                                              [str___\\e4/___bells]\r\nBridge (3): dm, #A... / rechts Bells Arp e..." },
    { index: 20, name: "Here I go agaim", comment: "Intro (1): G  D  C\r\n   em/h  am/c  G/h  am/a  C/g  D/a\r\nStr A (1): G  D  C                                  [str___\\c5/___solo]\r\nStr B (3): G   D/#f  em7/e  | C, hm | am  D  C/a  D\r\nRefr (2): G  C  D\r\nBridge (3): em  am... (Tief Akk | Oben Solo)\r\n\r\n" },
    { index: 21, name: "Bad case of Loving you", comment: "Organ 4 (normal) / 2 (dist)\r\nF, #A, C\r\n" },
    { index: 22, name: "It's only Love", comment: "Organ 2 (dist) / 4 (normal)\r\nIntro :2\r\nStr (2): dm, C, am, #A, F\r\nBridge (4): C, dm, #A, gm | C, dm, #A, C\r\n" },
    { index: 23, name: "Bad Name", comment: "Refr (1): cm, #G, #A, cm | #G, #A, #D cm |:2  ||  #G, #A\r\nBells (2): Lauf ab c Tonleiter\r\nBridge (3): F, #A cm | #A, F, G\r\nRefr\r\nStr: cm, #A :2\r\nSolo (3): wie Str, Ende: cm, #G, G\r\n\r\n\r\n" },
    { index: 24, name: "Nobody'S Wife", comment: "Refr A (1): #dm, H, #gm, #F, #C\r\nRefr B (2): #gm, #C, #F, H :2\r\nStr\r\nRefr\r\nStop: #gm, #C | single: #f, #g, #a, h\r\nSolo (3)\r\nref\r\nEnde (3): #gm, E" },
    { index: 25, name: "Crazy", comment: "Str (1): fm, #G, #A\r\nRefr (1): #C, #G, #D, #A\r\nBridge (2): Tonleiter ab f\r\n    f, g, #g, #a | #g, #a, c, d | c, d, #d, f | #d, f, g, #g\r\nHeavy (1): fm, #A   Bass: #d, f, #g | #d, f, #a" },
    { index: 26, name: "AC/DC", comment: "Highway to Hell (1)\r\n    [h-e-h] | [e-a] [a-d] [g-c] [#f-h] | [e-a] [d-a-d]\r\nHells Bells\r\n    (a1) | Lauf: e a d | g a c | g a d | a e\r\n    Choir (2): [d-a-d] [d-g-c] | [e-h-e] [e-a-d]\r\nTNT: e, g, a | a\r\nYou shook me all Night long; g, c, d, c\r\nH2Hell 2: [d-g] [g-c] [f-#a] [e-a]\r\nEnde: #a, g  (Deep Purple" },
    { index: 27, name: "Deep Purple", comment: "Black Night (1)\r\n    e d h g | Thema | d e |...\r\nBurn (1 und 2)\r\nPerfect Stranger (1 und 2)\r\n    (3)   | F C dm G dm C | G gm\r\nWoman from Tokio\r\nHighway Star\r\n     C D :3 | F G A" },
    { index: 28, name: "Dont stop believin", comment: "Intro [1]: ueber Strophe\r\nRefr [2]: a / #fm  A  H  #cm\r\n               e / H  E  H  E\r\n               a.... | H, E, H, #gm, A\r\nStr [2]: E, H, #cm, A | E, H, #gm, A\r\nSolo [3] organ\r\nEnde: H   E   H, #gm   A   E_" },
    { index: 29, name: "You oughta know", comment: "Intro (1): #f-#c-#f | #fm, h2\r\nStr (2): #fm, h2... \r\nBridge (3): #f | h\r\nRefr (2): #F  E  A  H\r\n....\r\nRefr (3): ...\r\nRefr Ende (4):\r\n" },
    { index: 30, name: "Africa", comment: "Intro (1): e7 (Drums) || A  #gm  #cm\r\nStr (1 -> 2): H  #F/#d  #gm  H/#f  \r\n                    A/e  #cm/#f  #gm  |  Intro  ||\r\n                    A  (H)  #cm / e2 (Sweep)\r\nRef (2): #fm  D  A  E \r\n              #fm  D  A | #cm  E  #fm  (2 -> 1) (E)\r\nSolo (3):" },
    { index: 31, name: "Beat It", comment: "Intro (1): dm C\r\nStr (1): dm C |:2  #A  C  dm  C\r\nRefr (1): dm C\r\nSolo (2): wie Ref" },
    { index: 32, name: "Heartbreaker", comment: "Intro [1]: em g-d-g |:2\r\n                            split ---- organ V brass ----      H, D, em\r\nStrophe [1]: em D C A | H, D     |:2\r\n                      em, D | C A H H     |:2\r\nRefr [2]: h-e   #c-#f   d-g   e-a   #f-h   g-c   #f-h   |:2\r\nSolo [3]: (e g d) e h a g #f e | d #c d e h a | g Tonleiter\r\n                f#, h |:4     h.....\r\nBreak [4]: e    h, c   g, a   h, a, g, #f, e    h, c   g, a   h..." },
    { index: 33, name: "I want it All", comment: "Intro (1): h__\r\n(2) A\r\nRefr. (2): hm  G  A || hm  A\r\nStr. (1):   hm  G  A || hm  A  //  D  A  G\r\nSolo: hm  G  A  E\r\n          G  D  G  D  A\r\n\r\n--> 34 +Value\r\nEnde: G" },
    { index: 34, name: "I want it All (ARPEGGIO)", comment: "Arp.: H:  h-#d-#f-h   [Finger _2: #d -> e -> #c]\r\n         E:   h-e-#f-h\r\n         #F:  h-#c-#f-a\r\n             :2 \r\n         || G  A" },
    { index: 35, name: "Purple Rain", comment: "1=Bells 2=Organ\r\n\r\nStr (1): #A gm F #D\r\nRefr (1): #D #A gm F\r\n\r\nEnde: #d, d, c, #a, g, f" },
    { index: 36, name: "Let it Rain", comment: "Str: hm  G  hm  G  |  \r\n       A  G  A  G\r\nRef: D  A  em  G  |:2\r\nBridge: A  G  A  G  |  \r\n              A  em, #fm  G\r\n\r\norgan?" },
    { index: 37, name: "Show must go on", comment: "Intro [1]: hm   G   #F   E\r\nStr_1 [2]: hm   G   #F   E\r\nStr_2 [2]: #cm   A   #G   #F\r\nBridge [3]: F   G   em   am,asus2/h  :2 | G, D__\r\nRef\r\nEnde [2]: em/g, esus4/g, G\r\nhh" },
    { index: 38, name: "Why can't this be Love", comment: "Intro: c\r\nBridge: #A F #D F G\r\nRef: C am F G\r\n        C  F G" },
    { index: 39, name: "Still lovin' you", comment: "Intro (1): gm F #D D7\r\nStrophe 1 (1): gm A D\r\nRefrain 1 (3): gm #D #A F :2 | gm cm D gm,F #D F gm\r\nStrophe 3 (2)\r\nStop: gm dm cm | D em7 | gm,F #D | F\r\nSolo  (1)\r\nEnde: gm,F #D | D ... gm" },
    { index: 40, name: "Juke Box Hero", comment: "Intro (1): #D #C  [#a-#c-#f]\r\nStr (1)\r\nBridge (2): #D #C ... #A\r\nRef (1): #D #F #C\r\nSolo (2): #D #C ... #A\r\nEnde (3): #d, f, #f, f, #d, #c, #f, #d" },
    { index: 41, name: "Break It Up", comment: "Intro [1]: LH [#d]   RH #f, f, #c ... (1) #d (2) #a |:2\r\nStr [2]:    #dm [#a-#f-g]   H    #A   H0 [h-d-f]    |:4\r\n                bass: f, d, #a, d, #d\r\nBridge     H [#f-h-#d]/#f   #F   H   #F\r\n                E   H/#d   #A\r\nRef [3]:    #dm   #am   H | #dm   #am   #gm |:4   #D__\r\nZwTeil [1]:  Bass: h, #a, #g, #c, #d, f, h, #c, h, #a, #g, #c\r\nEnde [3]:    Ref+Bass: #d, f, #f, h | #d, f, #f, #g" },
    { index: 42, name: "Witness", comment: "Str [1]: am\r\nRef [1]: C   D   am\r\nStr |:4\r\nRef:  C   D   am | C   D   G   em | C   D   am\r\nBridge [2]:   am   G   D   D, C\r\n                     am   G   D, #D   E\r\n" },
    { index: 43, name: "Lights", comment: "Intro: D hm D A h/A C | D D2 D\r\nStr: D hm C      1/8\r\nRef: hm G D ... | A | ... | hm C | D D2 D" },
    { index: 44, name: "You took the Word out..", comment: "Intro [4]: A {#c, h, a, #g}   D {#f_, e, d, #c, h}   E {h_}\r\nStr [1]:    A   D |:3 // G // Lauf: hm   #cm   D    E   A_\r\nStr           ..              Lauf |:3\r\nBridge [2]: hm   A   G   A   hm | D   A\r\n                   hm   A   G   A | #fm   E\r\nRef [1]: D   A   G   A | D   A\r\n              Lauf: hm   #cm   D   E | #fm   #gm   A   #cm | D\r\n               #fm   E\r\nEnde [3]:   (LH) a   e, d | (RH) A   D" },
    { index: 45, name: "Heros", comment: "Intro (1): #C | #F,#F2\r\nStr (1): #C #F\r\nRef (2): H H #C | #gm7 #dm #C | H #F, #C\r\nStr (3 Organ)\r\nEnde: Stop auf 2+ (her_os)\r\n#C___\r\n\r\n" },
    { index: 46, name: "What about Love", comment: "Intro [1]: cm   #A   #G   #A   |:2\r\nStr [2]:    cm   #G   #D   #A   |   #G   #G2\r\nRef [2]:   gm   #G   #A   |   Bells oben #A\r\nIntro [1]:\r\nRef [2]:    \r\nSolo [2]:  #G   #A |:3    cm   #A\r\nRef \r\nBridge [3]: unten: c-c | oben: #G   #A   C" },
    { index: 47, name: "Starship", comment: "combi -> eq -> PADs\r\n\r\nIntro [1]: bm  F | F, C, F/a, bm\r\n\r\nStr [3]: F  bm  #D  C, F" },
    { index: 48, name: "Sleep", comment: "Intro [1]: g, h, c...\r\nRef [1]:   C   D   em   C   D   G |:2 (erster Ref bis D)\r\nSolo [2]: C   D   G |:4\r\nBridge [3]:   em   D/#f    G   am | hm [d-#f-h]   C   D\r\nEnde [1]: C    D |:4   G___\r\n" },
    { index: 49, name: "Billy Idol", comment: "Intro [1]: G   D   em |:4    D, #fm, bm\r\nStr [1]:    bm   D   D4     lead: h - d\r\nRef [1]:   bm   D   D4     bells: h - d - h - a\r\nStr + Ref\r\nBridge [1]: G   D   em |:4    D, #fm, bm\r\nSolo ueber Ref\r\nKey [2]: links: h  a  g  e, d a, h" },
    { index: 50, name: "Learn to Fly", comment: "scene 1 (136)\r\nRef [1]: Bm   Gbm   E\r\nStr [2]:  Bm   Gbm   E\r\n      [1]   ... 1( G   A                     zu Intro\r\n      [1]   ... 2( G   A   E   G   A    \r\nBridge: Bm   G   D   E   |   G   A |:2\r\n       Str |:2\r\nEnde: G   A   E |:2\r\n           G   A   Bm" },
    { index: 51, name: "Manfred Mann", comment: "Intro [1]: dm   C   bB   C |:2 leer |:2 alles\r\n                dm   gm   Bass: g, f, a, bB, a, bB, c, f__\r\nStr [2]: F   F/e   bB   C   dm4 | gm   dm   bB   C |:2\r\n             bB   F   bB   F (a,g,f) | bB, C, F__\r\nPRef [1]: C tief oben lead\r\nRef[1]:    ... | dm   C   dm, C, bB\r\nStr + Ref\r\nSolo [2]: bB   C |:2    Key    bB   C   bB/d   C/e |:2\r\nBreak:    bB   F   #G\r\nEnde [4]:  F" },
    { index: 52, name: "Bon Jovy", comment: "Intro [1]:    #cm   E   A   E   B, B |:4     Transp:-1\r\nBridge [2]: #cm   E   #fm   A\r\nRef [2]:       #cm   A   E   B |:3\r\n                   #fm   A, A\r\nIntro\r\nSolo ueber Ref\r\nStop Ref\r\nIntro, Ref, Intro:6  Ende auf h" },
    { index: 53, name: "Zombie", comment: "Ref[3]: em   C   G   D/#f                                          (83)\r\n             em   C" },
    { index: 54, name: "Tainted Love", comment: "Ref [2]: am  C  F  dm                   Split: Bells | Pad | Lead\r\n              (oben)  a, c, f, c, d\r\nBridge: am  dm  F  dm |:2\r\n" },
    { index: 55, name: "No Doubt (JaG)", comment: "Str [1]: D\r\nRef:      hm   A   G, A   |:6\r\n             G   C\r\nSolo:    #f h d #c ...\r\nEnde:   bm" },
    { index: 56, name: "Bring me some water", comment: "Str [1]:  A5__  D  E\r\nRef [4]: A  G  D (Em7, D) |:7    E   A\r\nBridge [3]: F#  G  A |:3  hm  D  E" },
    { index: 57, name: "Smoke on the Water", comment: "Theme [1]: G5 (d - g)  F4 (f - bh)  C5 (g - c)\r\n                    G5  F4, Db5 (bA - bD)  C5\r\nStr:    G5 ...  F5  G5\r\nRef:   C5  Ab5  G5 (Lauf)\r\nSolo: G5  C5  G5 |:2    F5  C5" },
    { index: 58, name: "Mustang Sally", comment: "Str [3]: C  (G) | F  (C)  | [d-g], [d-f#], [c-f]" },
    { index: 59, name: "Who knew", comment: "Str [3]:  A (c#  e  Wechsel) | hm, A, E (g #f)\r\nRef [3]: D  E  F#m  A\r\nBridge: hm  F#  E" },
    { index: 64, name: "M.Miller 80ies MEDLEY", comment: "" },
    { index: 65, name: "M.Miller 80ies MEDLEY", comment: "" },
    { index: 66, name: "M.Miller 80ies MEDLEY", comment: "Intro[1]: Brass   #a, #g, #f, #d, #c\r\nStr [1]:   #D   #C\r\n      [2]: sweep auf c\r\nRef [3]: cm, #C, fm   | Organ: #D, #A\r\n       [2]: be-bh-be\r\nStr 2 [2]: g-#d-g   #g-f-#g   #a-g-#a | (RH) bE, #C\r\n                g-#c-g   f-#c-f   #d-#a-#d  |          bE, #C\r\nFlute [3]: ebm   H   g#m |:2\r\n                 RH: #d-#g-#a-#d" },
    { index: 67, name: "M.Miller 80ies MEDLEY", comment: "" },
    { index: 68, name: "M.Miller 80ies MEDLEY", comment: "" },
    { index: 69, name: "M.Miller 80ies MEDLEY", comment: "" },
    { index: 70, name: "Phil-X", comment: "\r\nRef: H7 [h-#d-#f-a]   C7 [c-e-G-#a]   H7   bH7   A7" },
  ],
};

// NOT real data: the real setlist above re-arranged into a gig the way the
// GIG / SET BREAK marker slots are meant to be used -- a GIG marker in slot
// 000, then the songs in their real order with a SET BREAK slot after every
// `songsPerSet` songs, renumbered 0..n like a Kronos Set List would be
// after inserting the markers. Names and comments are the real ones.
export function demoGigArrangement(setlist, { gigDate = "2025-10-28", songsPerSet = 24 } = {}) {
  const slots = [{ name: gigMarkerName(gigDate), comment: "" }];
  setlist.slots.forEach((slot, i) => {
    if (i > 0 && i % songsPerSet === 0) slots.push({ name: SET_BREAK_NAME, comment: "" });
    slots.push({ name: slot.name, comment: slot.comment });
  });
  return { name: setlist.name, slots: slots.map((slot, index) => ({ index, ...slot })) };
}

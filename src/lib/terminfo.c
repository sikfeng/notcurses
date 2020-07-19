#include <ncurses.h> // needed for some definitions, see terminfo(3ncurses)
#include "internal.h"

static bool
query_rgb(void){
  bool rgb = tigetflag("RGB") == 1;
  if(!rgb){
    // RGB terminfo capability being a new thing (as of ncurses 6.1), it's not commonly found in
    // terminal entries today. COLORTERM, however, is a de-facto (if imperfect/kludgy) standard way
    // of indicating TrueColor support for a terminal. The variable takes one of two case-sensitive
    // values:
    //
    //   truecolor
    //   24bit
    //
    // https://gist.github.com/XVilka/8346728#true-color-detection gives some more information about
    // the topic
    //
    const char* cterm = getenv("COLORTERM");
    rgb = cterm && (strcmp(cterm, "truecolor") == 0 || strcmp(cterm, "24bit") == 0);
  }
  return rgb;
}

int terminfostr(char** gseq, const char* name){
  char* seq;
  if(gseq == NULL){
    gseq = &seq;
  }
  *gseq = tigetstr(name);
  if(*gseq == NULL || *gseq == (char*)-1){
    *gseq = NULL;
    return -1;
  }
  // terminfo syntax allows a number N of milliseconds worth of pause to be
  // specified using $<N> syntax. this is then honored by tputs(). but we don't
  // use tputs(), instead preferring the much faster stdio+tiparm(). to avoid
  // dumping "$<N>" sequences all over stdio, we chop them out.
  char* pause;
  if( (pause = strchr(*gseq, '$')) ){
    *pause = '\0';
  }
  return 0;
}

int interrogate_terminfo(tinfo* ti){
  memset(ti, 0, sizeof(*ti));
  ti->RGBflag = query_rgb();
  if((ti->colors = tigetnum("colors")) <= 0){
    ti->colors = 1;
    ti->CCCflag = false;
    ti->RGBflag = false;
    ti->initc = NULL;
  }else{
    terminfostr(&ti->initc, "initc");
    if(ti->initc){
      ti->CCCflag = tigetflag("ccc") == 1;
    }else{
      ti->CCCflag = false;
    }
  }
  // check that the terminal provides cursor addressing (absolute movement)
  terminfostr(&ti->cup, "cup");
  if(ti->cup == NULL){
    fprintf(stderr, "Required terminfo capability 'cup' not defined\n");
    return -1;
  }
  // check that the terminal provides automatic margins
  ti->AMflag = tigetflag("am") == 1;
  if(!ti->AMflag){
    fprintf(stderr, "Required terminfo capability 'am' not defined\n");
    return -1;
  }
  terminfostr(&ti->civis, "civis"); // cursor invisible
  terminfostr(&ti->cnorm, "cnorm"); // cursor normal (undo civis/cvvis)
  terminfostr(&ti->standout, "smso"); // begin standout mode
  terminfostr(&ti->uline, "smul");    // begin underline mode
  terminfostr(&ti->reverse, "rev");   // begin reverse video mode
  terminfostr(&ti->blink, "blink");   // turn on blinking
  terminfostr(&ti->dim, "dim");       // turn on half-bright mode
  terminfostr(&ti->bold, "bold");     // turn on extra-bright mode
  terminfostr(&ti->italics, "sitm");  // begin italic mode
  terminfostr(&ti->italoff, "ritm");  // end italic mode
  terminfostr(&ti->sgr, "sgr");       // define video attributes
  terminfostr(&ti->sgr0, "sgr0");     // turn off all video attributes
  terminfostr(&ti->op, "op");         // restore defaults to default pair
  terminfostr(&ti->oc, "oc");         // restore defaults to all colors
  terminfostr(&ti->home, "home");     // home the cursor
  terminfostr(&ti->clearscr, "clear");// clear screen, home cursor
  terminfostr(&ti->cleareol, "el");   // clear to end of line
  terminfostr(&ti->clearbol, "el1");  // clear to beginning of line
  terminfostr(&ti->cuu, "cuu"); // move N up
  terminfostr(&ti->cud, "cud"); // move N down
  terminfostr(&ti->hpa, "hpa"); // set horizontal position
  terminfostr(&ti->vpa, "vpa"); // set verical position
  terminfostr(&ti->cuf, "cuf"); // n non-destructive spaces
  terminfostr(&ti->cub, "cub"); // n non-destructive backspaces
  terminfostr(&ti->cuf1, "cuf1"); // non-destructive space
  terminfostr(&ti->cub1, "cub1"); // non-destructive backspace
  // Some terminals cannot combine certain styles with colors. Don't advertise
  // support for the style in that case.
  int nocolor_stylemask = tigetnum("ncv");
  if(nocolor_stylemask > 0){
    if(nocolor_stylemask & WA_STANDOUT){ // ncv is composed of terminfo bits, not ours
      ti->standout = NULL;
    }
    if(nocolor_stylemask & WA_UNDERLINE){
      ti->uline = NULL;
    }
    if(nocolor_stylemask & WA_REVERSE){
      ti->reverse = NULL;
    }
    if(nocolor_stylemask & WA_BLINK){
      ti->blink = NULL;
    }
    if(nocolor_stylemask & WA_DIM){
      ti->dim = NULL;
    }
    if(nocolor_stylemask & WA_BOLD){
      ti->bold = NULL;
    }
    if(nocolor_stylemask & WA_ITALIC){
      ti->italics = NULL;
    }
  }
  terminfostr(&ti->getm, "getm"); // get mouse events
  // Not all terminals support setting the fore/background independently
  terminfostr(&ti->setaf, "setaf"); // set forground color
  terminfostr(&ti->setab, "setab"); // set background color
  terminfostr(&ti->smkx, "smkx");   // enable keypad transmit
  terminfostr(&ti->rmkx, "rmkx");   // disable keypad transmit
  // if the keypad neen't be explicitly enabled, smkx is not present
  if(ti->smkx){
    if(putp(tiparm(ti->smkx)) != OK){
      fprintf(stderr, "Error entering keypad transmit mode\n");
      return -1;
    }
  }
  return 0;
}

int cursor_yx_get(int ttyfd, int* y, int* x){
  if(write(ttyfd, "\x1b[6n", 4) != 4){
    return -1;
  }
  bool done = false;
  enum { // what we expect now
    CURSOR_ESC, // 27 (0x1b)
    CURSOR_LSQUARE,
    CURSOR_ROW, // delimited by a semicolon
    CURSOR_COLUMN,
    CURSOR_R,
  } state = CURSOR_ESC;
  int row = 0, column = 0;
  char in;
  while(read(ttyfd, &in, 1) == 1){
    bool valid = false;
    switch(state){
      case CURSOR_ESC: valid = (in == '\x1b'); state = CURSOR_LSQUARE; break;
      case CURSOR_LSQUARE: valid = (in == '['); state = CURSOR_ROW; break;
      case CURSOR_ROW:
        if(isdigit(in)){
          row *= 10;
          row += in - '0';
          valid = true;
        }else if(in == ';'){
          state = CURSOR_COLUMN;
          valid = true;
        }
        break;
      case CURSOR_COLUMN:
        if(isdigit(in)){
          column *= 10;
          column += in - '0';
          valid = true;
        }else if(in == 'R'){
          state = CURSOR_R;
          valid = true;
        }
        break;
      case CURSOR_R: default: // logical error, whoops
        break;
    }
    if(!valid){
      fprintf(stderr, "Unexpected result from terminal: %d\n", in);
      break;
    }
    if(state == CURSOR_R){
      done = true;
      break;
    }
  }
  if(!done){
    return -1;
  }
  if(y){
    *y = row;
  }
  if(x){
    *x = column;
  }
  return 0;
}

// this escape actually queries a number of capabilties; we ought expose them
// all, i am thinking.
static int
interrogate_sixel(int ttyfd){
  if(ttyfd < 0){
    return -1;
  }
  #define ESCAPESEQ "\x1b[c"
  #define CAPABILITY_SIXEL 4
  const size_t slen = strlen(ESCAPESEQ);
  ssize_t wlen = write(ttyfd, "\x1B[c", slen);
  if(wlen < 0 || (size_t)wlen != slen){
    return -1;
  }
  bool done = false;
  bool havesixel = false;
  enum { // what we expect now
    CAPS_ESC, // 27 (0x1b)
    CAPS_LSQUARE,
    CAPS_QMARK, // 63
    CAPS_DIGITS, // delimited by a semicolon
    CAPS_MUSTDIGIT, // just had semicolon
    CAPS_C,
  } state = CAPS_ESC;
  int capid = 0;
  char in;
  ssize_t r;
  while((r = read(ttyfd, &in, 1)) == 1 || (r < 0 && errno == EAGAIN)){
    if(r < 0 && errno == EAGAIN){
      continue;
    }
    bool valid = false;
    switch(state){
      case CAPS_ESC: valid = (in == '\x1b'); state = CAPS_LSQUARE; break;
      case CAPS_LSQUARE: valid = (in == '['); state = CAPS_QMARK; break;
      case CAPS_QMARK: valid = (in == '?'); state = CAPS_DIGITS; break;
      case CAPS_DIGITS: case CAPS_MUSTDIGIT:
        if(isdigit(in)){
          capid *= 10;
          capid += in - '0';
          valid = true;
          state = CAPS_DIGITS;
        }else if(in == ';'){
          if(state == CAPS_MUSTDIGIT){
            break;
          }
//fprintf(stderr, "capid: %d\n", capid);
          if(capid == CAPABILITY_SIXEL){
            havesixel = true;
          }
          capid = 0;
          valid = true;
          state = CAPS_MUSTDIGIT;
        }else if(in == 'c'){
          state = CAPS_C;
          valid = true;
        }
        break;
      case CAPS_C: default: // logical error, whoops
        break;
    }
    if(!valid){
      break;
    }
    if(state == CAPS_C){
      done = true;
      break;
    }
  }
  if(!done){
    return -1;
  }
  if(!havesixel){
    return -1;
  }
  return 0;
  #undef CAPABILITY_SIXEL
  #undef ESCAPESEQ
}

bool notcurses_cansixel(notcurses* nc){
  bool sixel = false;
  pthread_mutex_lock(&nc->sixellock);
  if(nc->sixel == SIXEL_UNVERIFIED){
    if(interrogate_sixel(nc->ttyfd)){
      nc->sixel = SIXEL_VERIFIED_FALSE;
    }else{
      nc->sixel = SIXEL_VERIFIED_TRUE;
    }
  }
  sixel = (nc->sixel == SIXEL_VERIFIED_TRUE);
  pthread_mutex_unlock(&nc->sixellock);
  return sixel;
}

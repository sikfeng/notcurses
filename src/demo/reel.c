#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include "demo.h"

#define INITIAL_TABLET_COUNT 4

static pthread_mutex_t renderlock = PTHREAD_MUTEX_INITIALIZER;

// FIXME ought just be an unordered_map
typedef struct tabletctx {
  pthread_t tid;
  struct ncreel* pr;
  struct nctablet* t;
  int lines;
  unsigned rgb;
  unsigned id;
  struct tabletctx* next;
  pthread_mutex_t lock;
} tabletctx;

static void
kill_tablet(tabletctx** tctx){
  tabletctx* t = *tctx;
  if(t){
    if(pthread_cancel(t->tid)){
      fprintf(stderr, "Warning: error sending pthread_cancel (%s)\n", strerror(errno));
    }
    if(pthread_join(t->tid, NULL)){
      fprintf(stderr, "Warning: error joining pthread (%s)\n", strerror(errno));
    }
    ncreel_del(t->pr, t->t);
    *tctx = t->next;
    pthread_mutex_destroy(&t->lock);
    free(t);
  }
}

static int
kill_active_tablet(struct ncreel* pr, tabletctx** tctx){
  struct nctablet* focused = ncreel_focused(pr);
  tabletctx* t;
  while( (t = *tctx) ){
    if(t->t == focused){
      *tctx = t->next; // pull it out of the list
      t->next = NULL; // finish splicing it out
      break;
    }
    tctx = &t->next;
  }
  if(t == NULL){
    return -1; // wasn't present in our list, wacky
  }
  kill_tablet(&t);
  return 0;
}

// We need write in reverse order (since only the bottom will be seen, if we're
// partially off-screen), but also leave unused space at the end (since
// wresize() only keeps the top and left on a shrink).
static int
tabletup(struct ncplane* w, int begx, int begy, int maxx, int maxy,
         tabletctx* tctx, int rgb){
  char cchbuf[2];
  cell c = CELL_TRIVIAL_INITIALIZER;
  int y, idx;
  idx = tctx->lines;
  if(maxy - begy > tctx->lines){
    maxy -= (maxy - begy - tctx->lines);
  }
/*fprintf(stderr, "-OFFSET BY %d (%d->%d)\n", maxy - begy - tctx->lines,
        maxy, maxy - (maxy - begy - tctx->lines));*/
  for(y = maxy ; y >= begy ; --y, rgb += 16){
    snprintf(cchbuf, sizeof(cchbuf) / sizeof(*cchbuf), "%x", idx % 16);
    cell_load(w, &c, cchbuf);
    if(cell_set_fg_rgb(&c, (rgb >> 16u) % 0xffu, (rgb >> 8u) % 0xffu, rgb % 0xffu)){
      return -1;
    }
    int x;
    for(x = begx ; x <= maxx ; ++x){
      if(ncplane_putc_yx(w, y, x, &c) <= 0){
        return -1;
      }
    }
    cell_release(w, &c);
    if(--idx == 0){
      break;
    }
  }
// fprintf(stderr, "tabletup done%s at %d (%d->%d)\n", idx == 0 ? " early" : "", y, begy, maxy);
  return tctx->lines - idx;
}

static int
tabletdown(struct ncplane* w, int begx, int begy, int maxx, int maxy,
           tabletctx* tctx, unsigned rgb){
  char cchbuf[2];
  cell c = CELL_TRIVIAL_INITIALIZER;
  int y;
  for(y = begy ; y <= maxy ; ++y, rgb += 16){
    if(y - begy >= tctx->lines){
      break;
    }
    snprintf(cchbuf, sizeof(cchbuf) / sizeof(*cchbuf), "%x", y % 16);
    cell_load(w, &c, cchbuf);
    if(cell_set_fg_rgb(&c, (rgb >> 16u) % 0xffu, (rgb >> 8u) % 0xffu, rgb % 0xffu)){
      return -1;
    }
    int x;
    for(x = begx ; x <= maxx ; ++x){
      if(ncplane_putc_yx(w, y, x, &c) <= 0){
        return -1;
      }
    }
    cell_release(w, &c);
  }
  return y - begy;
}

static int
tabletdraw(struct nctablet* t, int begx, int begy, int maxx, int maxy, bool cliptop){
  struct ncplane* p = nctablet_ncplane(t);
  tabletctx* tctx = nctablet_userptr(t);
  pthread_mutex_lock(&tctx->lock);
  unsigned rgb = tctx->rgb;
  int ll;
  if(cliptop){
    ll = tabletup(p, begx, begy, maxx, maxy, tctx, rgb);
  }else{
    ll = tabletdown(p, begx, begy, maxx, maxy, tctx, rgb);
  }
  ncplane_set_fg_rgb(p, 242, 242, 242);
  if(ll){
    int summaryy = begy;
    if(cliptop){
      if(ll == maxy - begy + 1){
        summaryy = ll - 1;
      }else{
        summaryy = ll;
      }
    }
    ncplane_styles_on(p, NCSTYLE_BOLD);
    if(ncplane_printf_yx(p, summaryy, begx, "[#%u %d line%s %u/%u] ",
                         tctx->id, tctx->lines, tctx->lines == 1 ? "" : "s",
                         begy, maxy) < 0){
      pthread_mutex_unlock(&tctx->lock);
      return -1;
    }
    ncplane_styles_off(p, NCSTYLE_BOLD);
  }
//fprintf(stderr, "  \\--> callback for %d, %d lines (%d/%d -> %d/%d) dir: %s wrote: %d\n", tctx->id, tctx->lines, begy, begx, maxy, maxx, cliptop ? "up" : "down", ll);
  pthread_mutex_unlock(&tctx->lock);
  return ll;
}

// Each tablet has an associated thread which will periodically send update
// events for its tablet.
static void*
tablet_thread(void* vtabletctx){
  static int MINSECONDS = 0;
  tabletctx* tctx = vtabletctx;
  while(true){
    struct timespec ts;
    ts.tv_sec = random() % 2 + MINSECONDS;
    ts.tv_nsec = random() % 1000000000;
    nanosleep(&ts, NULL);
    int action = random() % 5;
    pthread_mutex_lock(&tctx->lock);
    if(action < 2){
      if((tctx->lines -= (action + 1)) < 1){
        tctx->lines = 1;
      }
    }else if(action > 2){
      if((tctx->lines += (action - 2)) < 1){
        tctx->lines = 1;
      }
    }
    pthread_mutex_unlock(&tctx->lock);
    pthread_mutex_lock(&renderlock);
    if(nctablet_ncplane(tctx->t)){
      ncreel_redraw(tctx->pr);
      demo_render(ncplane_notcurses(nctablet_ncplane(tctx->t)));
    }
    pthread_mutex_unlock(&renderlock);
  }
  return tctx;
}

static tabletctx*
new_tabletctx(struct ncreel* pr, unsigned *id){
  tabletctx* tctx = malloc(sizeof(*tctx));
  if(tctx == NULL){
    return NULL;
  }
  pthread_mutex_init(&tctx->lock, NULL);
  tctx->pr = pr;
  tctx->lines = random() % 10 + 1; // FIXME a nice gaussian would be swell
  tctx->rgb = random() % (1u << 24u);
  tctx->id = ++*id;
  if((tctx->t = ncreel_add(pr, NULL, NULL, tabletdraw, tctx)) == NULL){
    pthread_mutex_destroy(&tctx->lock);
    free(tctx);
    return NULL;
  }
  if(pthread_create(&tctx->tid, NULL, tablet_thread, tctx)){
    pthread_mutex_destroy(&tctx->lock);
    free(tctx);
    return NULL;
  }
  return tctx;
}

static wchar_t
handle_input(struct notcurses* nc, const struct timespec* deadline,
             ncinput* ni){
  int64_t deadlinens = timespec_to_ns(deadline);
  struct timespec pollspec, cur;
  clock_gettime(CLOCK_MONOTONIC, &cur);
  int64_t curns = timespec_to_ns(&cur);
  if(curns > deadlinens){
    return 0;
  }
  ns_to_timespec(deadlinens - curns, &pollspec);
  wchar_t r = demo_getc(nc, &pollspec, ni);
  return r;
}

static int
ncreel_demo_core(struct notcurses* nc){
  tabletctx* tctxs = NULL;
  bool aborted = false;
  int x = 8, y = 4;
  int dimy, dimx;
  struct ncplane* std = notcurses_stddim_yx(nc, &dimy, &dimx);
  struct ncplane* w = ncplane_new(nc, dimy - 12, dimx - 16, y, x, NULL);
  if(w == NULL){
    return -1;
  }
  ncreel_options popts = {
    .bordermask = 0,
    .borderchan = 0,
    .tabletchan = 0,
    .focusedchan = 0,
    .bgchannel = 0,
    .flags = NCREEL_OPTION_INFINITESCROLL | NCREEL_OPTION_CIRCULAR,
  };
  channels_set_fg_rgb(&popts.focusedchan, 58, 150, 221);
  channels_set_bg_rgb(&popts.focusedchan, 97, 214, 214);
  channels_set_fg_rgb(&popts.tabletchan, 19, 161, 14);
  channels_set_bg_rgb(&popts.borderchan, 0, 0, 0);
  channels_set_fg_rgb(&popts.borderchan, 136, 23, 152);
  channels_set_bg_rgb(&popts.borderchan, 0, 0, 0);
  if(channels_set_fg_alpha(&popts.bgchannel, CELL_ALPHA_TRANSPARENT)){
    ncplane_destroy(w);
    return -1;
  }
  if(channels_set_bg_alpha(&popts.bgchannel, CELL_ALPHA_TRANSPARENT)){
    ncplane_destroy(w);
    return -1;
  }
  struct ncreel* pr = ncreel_create(w, &popts);
  if(pr == NULL){
    ncplane_destroy(w);
    return -1;
  }
  // Press a for a new nc above the current, c for a new one below the
  // current, and b for a new block at arbitrary placement.
  ncplane_styles_on(std, NCSTYLE_BOLD | NCSTYLE_ITALIC);
  ncplane_set_fg_rgb(std, 58, 150, 221);
  ncplane_set_bg_default(std);
  ncplane_printf_yx(std, 1, 2, "a, b, c create tablets, DEL deletes.");
  ncplane_styles_off(std, NCSTYLE_BOLD | NCSTYLE_ITALIC);
  // FIXME clrtoeol();
  struct timespec deadline;
  clock_gettime(CLOCK_MONOTONIC, &deadline);
  ns_to_timespec((timespec_to_ns(&demodelay) * 5) + timespec_to_ns(&deadline),
                 &deadline);
  unsigned id = 0;
  struct tabletctx* newtablet;
  // Make an initial number of tablets suitable for the screen's height
  while(id < dimy / 8u){
    newtablet = new_tabletctx(pr, &id);
    if(newtablet == NULL){
      ncreel_destroy(pr);
      return -1;
    }
    newtablet->next = tctxs;
    tctxs = newtablet;
  }
  do{
    ncplane_styles_set(std, 0);
    ncplane_set_fg_rgb(std, 197, 15, 31);
    int count = ncreel_tabletcount(pr);
    ncplane_styles_on(std, NCSTYLE_BOLD);
    ncplane_printf_yx(std, 2, 2, "%d tablet%s", count, count == 1 ? "" : "s");
    ncplane_styles_off(std, NCSTYLE_BOLD);
    // FIXME wclrtoeol(w);
    ncplane_set_fg_rgb(std, 0, 55, 218);
    wchar_t rw;
    ncinput ni;
    pthread_mutex_lock(&renderlock);
    ncreel_redraw(pr);
    int renderret;
    renderret = demo_render(nc);
    pthread_mutex_unlock(&renderlock);
    if(renderret){
      ncreel_destroy(pr);
      return renderret;
    }
    if((rw = handle_input(nc, &deadline, &ni)) == (wchar_t)-1){
      break;
    }
    // FIXME clrtoeol();
    newtablet = NULL;
    switch(rw){
      case 'a': newtablet = new_tabletctx(pr, &id); break;
      case 'b': newtablet = new_tabletctx(pr, &id); break;
      case 'c': newtablet = new_tabletctx(pr, &id); break;
      case 'k': ncreel_prev(pr); break;
      case 'j': ncreel_next(pr); break;
      case 'q': aborted = true; break;
      case NCKEY_UP: ncreel_prev(pr); break;
      case NCKEY_DOWN: ncreel_next(pr); break;
      case NCKEY_LEFT:
        ncplane_yx(ncreel_plane(pr), &y, &x);
        ncplane_move_yx(ncreel_plane(pr), y, x - 1);
        break;
      case NCKEY_RIGHT:
        ncplane_yx(ncreel_plane(pr), &y, &x);
        ncplane_move_yx(ncreel_plane(pr), y, x + 1);
        break;
      case NCKEY_DEL: kill_active_tablet(pr, &tctxs); break;
      case NCKEY_RESIZE: notcurses_render(nc); break;
      default: ncplane_printf_yx(std, 3, 2, "Unknown keycode (0x%x)\n", rw); break;
    }
    if(newtablet){
      newtablet->next = tctxs;
      tctxs = newtablet;
    }
    struct timespec cur;
    clock_gettime(CLOCK_MONOTONIC, &cur);
    if(timespec_subtract_ns(&cur, &deadline) >= 0){
      break;
    }
    dimy = ncplane_dim_y(w);
  }while(!aborted);
  while(tctxs){
    kill_tablet(&tctxs);
  }
  if(ncreel_destroy(pr)){
    fprintf(stderr, "Error destroying ncreel\n");
    return -1;
  }
  return aborted ? 1 : 0;
}

int reel_demo(struct notcurses* nc){
  ncplane_greyscale(notcurses_stdplane(nc));
  int ret = ncreel_demo_core(nc);
  return ret;
}

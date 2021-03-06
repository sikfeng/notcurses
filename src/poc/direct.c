#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/ioctl.h>
#include <notcurses/direct.h>

// can we leave what was already on the screen there? (narrator: it seems not)
int main(void){
  if(!setlocale(LC_ALL, "")){
    fprintf(stderr, "Couldn't set locale\n");
    return EXIT_FAILURE;
  }
  struct ncdirect* n; // see bug #391
  if((n = ncdirect_init(NULL, stdout)) == NULL){
    return EXIT_FAILURE;
  }
  int dimy = ncdirect_dim_y(n);
  int dimx = ncdirect_dim_x(n);
  for(int y = 0 ; y < dimy ; ++y){
    for(int x = 0 ; x < dimx ; ++x){
      printf("X");
    }
  }
  fflush(stdout);
  int ret = 0;
  ret |= ncdirect_fg(n, 0xff8080);
  ret |= ncdirect_styles_on(n, NCSTYLE_STANDOUT);
  printf(" erp erp \n");
  ret |= ncdirect_fg(n, 0x80ff80);
  printf(" erp erp \n");
  ret |= ncdirect_styles_off(n, NCSTYLE_STANDOUT);
  printf(" erp erp \n");
  ret |= ncdirect_fg(n, 0xff8080);
  printf(" erp erp \n");
  ret |= ncdirect_cursor_right(n, dimx / 2);
  ret |= ncdirect_cursor_up(n, dimy / 2);
  printf(" erperperp! \n");
  int y = -420, x = -420;
  // FIXME try a push/pop
  if(ncdirect_cursor_yx(n, &y, &x) == 0){
    printf("\n\tRead cursor position: y: %d x: %d\n", y, x);
    y += 2; // we just went down two lines
    while(y > 3){
      const int up = y >= 3 ? 3 : y;
      if(ncdirect_cursor_up(n, up)){
        return EXIT_FAILURE;
      }
      fflush(stdout);
      y -= up;
      int newy;
      if(ncdirect_cursor_yx(n, &newy, NULL)){
        return EXIT_FAILURE;
      }
      if(newy != y){
        fprintf(stderr, "Expected %d, got %d\n", y, newy);
        return EXIT_FAILURE;
      }
      printf("\n\tRead cursor position: y: %d x: %d\n", newy, x);
      y += 2;
    }
  }else{
    ret = -1;
  }
  return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

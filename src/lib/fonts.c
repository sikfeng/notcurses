#include <pango/pangocairo.h>
#include "internal.h"

int fonts_init(void){
  PangoFontMap* fontmap = pango_cairo_font_map_get_default();
  if(fontmap == NULL){
    return -1;
  }
  PangoContext* ctx = pango_font_map_create_context(fontmap);
  if(ctx == NULL){
    return -1;
  }
  // FIXME
  return 0;
}

#include <stdio.h>
#include "v32mouse.h"
static uint32_t t; static uint16_t sent; 
uint32_t v32io_millis(void){return t;}
bool v32io_delivered(uint16_t b){return sent==b;}
v32io_mode_t v32io_mode(void){return V32IO_MODE_MOUSE;}
static uint16_t run(int ms){ uint16_t b=0; for(int i=0;i<ms;i++){t++; b=mouse_task(); sent=b;} return b; }
int main(void){
  int fails=0;
  mouse_reset();
  printf("rest: %03x\n", run(10));
  mouse_input(7,0,0); uint16_t b=run(1000); printf("chord 1s (normal, buttons shown): %03x setup=%d\n", b, mouse_setup_mode());
  b=run(1100); printf("chord 2.1s: %03x setup=%d\n", b, mouse_setup_mode()); fails += !mouse_setup_mode();
  mouse_input(0,0,0); run(50);
  for(int k=0;k<3;k++){ mouse_input(1,0,0); b=run(50); printf(" click %d -> %03x\n",k,b); fails += b != (1u<<k); mouse_input(0,0,0); b=run(50); fails += b!=0; }
  mouse_input(2,0,0); run(50); mouse_input(0,0,0); run(50);  // back one
  mouse_input(1,0,0); b=run(50); printf(" after right click -> %03x\n",b); fails += b != (1u<<2); mouse_input(0,0,0); run(50);
  mouse_input(7,0,0); run(2100); printf("toggle out: setup=%d buttons=%03x (held, ignored)\n", mouse_setup_mode(), run(10)); fails += mouse_setup_mode();
  mouse_input(0,0,0); run(50); mouse_input(1,0,0); b=run(50); printf("left after release: %03x\n", b); fails += !(b & V32BTN_A);
  mouse_input(0,0,0); run(50);
  // quick click 3 ms long must still last >= 25 ms
  mouse_input(1,0,0); run(3); mouse_input(0,0,0); int on=0; for(int i=0;i<60;i++){t++; sent=mouse_task(); if(sent & V32BTN_A) on++;} printf("3ms click shown for %d ms\n", on+3); fails += on+3 < 25;
  printf("%s\n", fails? "FAIL":"PASS"); return fails;
}

#include <PNGdecoder/PNGdecoder.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_pixels.h>
#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_video.h>

const int SCREEN_WIDTH = 1024;
const int SCREEN_HEIGHT = 800;

void terminate(int32_t code) {
  SDL_Quit();
  exit(code);
}

SDL_Surface * create_transparency_surface(uint8_t size){
  SDL_Surface * basic_tr_back_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, size*2, size*2, 32, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
  SDL_Rect rect;
  rect.h = rect.w = size;

  rect.x = rect.y = 0;
  SDL_FillRect(basic_tr_back_surface, &rect, SDL_MapRGBA(basic_tr_back_surface->format, 0xAA, 0xAA, 0xAA, 0xFF));
  rect.x += 5;
  SDL_FillRect(basic_tr_back_surface, &rect, SDL_MapRGBA(basic_tr_back_surface->format, 0xFF, 0xFF, 0xFF, 0xFF));
  rect.y += 5;
  SDL_FillRect(basic_tr_back_surface, &rect, SDL_MapRGBA(basic_tr_back_surface->format, 0xAA, 0xAA, 0xAA, 0xFF));
  rect.x -= 5;
  SDL_FillRect(basic_tr_back_surface, &rect, SDL_MapRGBA(basic_tr_back_surface->format, 0xFF, 0xFF, 0xFF, 0xFF));

  return basic_tr_back_surface;
}

void update_background(SDL_Surface * background_surface, SDL_Surface * basic_block){
  SDL_Rect rect;
  rect.h = basic_block->h;
  rect.w = basic_block->w;

  for(rect.x = 0; rect.x < background_surface->w; rect.x += basic_block->w){
    for(rect.y = 0; rect.y < background_surface->h; rect.y += basic_block->h){
      SDL_BlitSurface(basic_block, NULL, background_surface, &rect);
    }
  }

}

void fit_to_screen(SDL_Rect * rect, SDL_Surface * screen_surface, SDL_Surface * png_surface){
  if(png_surface->w <= screen_surface->w){
    rect->x = screen_surface->w/2 - png_surface->w/2;
    rect->w = png_surface->w;
  } else {
    rect->x = 0;
    rect->w = screen_surface->w;
  }
  if(png_surface->h <= screen_surface->h){
    rect->y = screen_surface->h/2 - png_surface->h/2;
    rect->h = png_surface->h;
  } else {
    rect->y = 0;
    rect->h = screen_surface->h;
  }
}

int main(int argc, char * argv[]){
  if(argc < 2){
    printf("Usage: %s <filename>\n", argv[0]);
    return -1;
  }

  const char * filename = argv[1];
  
  if(SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("Error initializing SDL: %s\n", SDL_GetError());
    return -2;
  }

  SDL_Window * window = SDL_CreateWindow("PNGdecoder demo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

  if(window == NULL){
    printf("Can't create window: %s\n", SDL_GetError()); terminate(-2);
  }

  PNGdecoder_PNG * png = NULL;
  PNGdecoder_result result = PNGdecoder_openPNG(filename, &png);

  if(result != PNGDECODER_OK){
    printf("Can't open %s: %s\n", filename, PNGdecoder_strerror(result));
    terminate(-3);
  }

  PNGdecoder_raster_RGBA8_t * raster = PNGdecoder_as_RGBA8(png);

  SDL_Surface * png_surface = SDL_CreateRGBSurface(SDL_SWSURFACE, raster->width, raster->height, 32, 0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF);
  
  uint32_t * current_pixel = (uint32_t *) png_surface->pixels;
  uint32_t i, j;
  for(i = 0; i < raster->height; i++){
    for(j = 0; j < raster->width; j++){
      *current_pixel = SDL_MapRGBA(png_surface->format, raster->raster[i*raster->width + j].R,
          raster->raster[i*raster->width + j].G, raster->raster[i*raster->width + j].B, raster->raster[i*raster->width + j].A);
      current_pixel++;
    }
  }

  PNGdecoder_raster_free(raster, PNGDECODER_RASTER_RGBA_8);
  PNGdecoder_free(png);

  SDL_Surface * screen_surface = SDL_GetWindowSurface(window);
  SDL_Surface * basic_tr_back_surface = create_transparency_surface(5);
 
  SDL_Event e;
  uint8_t quit = 0;
  SDL_Rect blit_rect;
  fit_to_screen(&blit_rect, screen_surface, png_surface);

  while(!quit){
    while(SDL_PollEvent(&e)){

      if(e.type == SDL_QUIT)
        quit = 1;

      if(e.type == SDL_WINDOWEVENT){
        if(e.window.event == SDL_WINDOWEVENT_EXPOSED){
          screen_surface = SDL_GetWindowSurface(window);
          update_background(screen_surface, basic_tr_back_surface);
          fit_to_screen(&blit_rect, screen_surface, png_surface);
          
          //SDL_BlitSurface(png_surface, NULL, screen_surface, NULL);
          SDL_BlitScaled(png_surface, NULL, screen_surface, &blit_rect);
          SDL_UpdateWindowSurface(window);
        }
      }

    }

    SDL_Delay(1000/30);
  }

  SDL_FreeSurface(png_surface);
  SDL_FreeSurface(basic_tr_back_surface);
  SDL_DestroyWindow(window);
  terminate(0);
}

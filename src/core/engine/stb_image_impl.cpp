// stb_image_impl.cpp -- the one stb_image implementation of the core code (game_assets.cpp's sprite
// loading, gltf_model.cpp's textures). Its own file, so a test that only decodes images (e.g.
// gltf_model_test) does not link the game's asset loader -- and through it the bgfx renderer.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

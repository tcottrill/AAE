#pragma once
#ifndef _texture_loader_shim_h_
#define _texture_loader_shim_h_

#include <allegro.h>
#include "alleggl.h"

GLuint load_texture(const char* filename, const char* archname, int numcomponents, int filter);

#endif

#pragma once

#ifndef GL_SHADER_H
#define GL_SHADER_H

#include <allegro.h>
#include "alleggl.h"

//Shader Handles
extern GLuint fragBlur;
extern GLuint fragMulti;
extern GLuint MotionBlur;

int init_shader();

#endif

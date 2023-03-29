
#ifndef SHADER_H
#define SHADER_H


#include "aaemain.h"


int CompileShaders(void);
char *ReadTextFile(const char *fileName);
void WriteShaderError(GLhandleARB obj, const char *shaderName);


#endif 
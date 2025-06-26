
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include "fonts.h"
#include "glcode.h"
#include "gl_texturing.h"

static GLuint base = 0;
static GLuint font_tex[2];

float fwidth[] = { 9,8,8,18,18,22,19,5,8,8,12,20,5,9,5,10,19,13,17,16,20,16,18,15,18,17,5,5,20,20,20,17,24,17,18,16,19,16,
                16,18,17,7,14,16,13,23,19,19,18,19,18,18,16,18,19,27,17,17,18,8,10,8,24,12,5,15,15,13,15,13,10,15,14,6,
                8,14,6,21,14,15,15,15,11,14,11,14,16,22,14,14,14,12,12,12,20,9,9,9,9,19,9,24,12,12,12,32,9,10,29,9,9,9,
                9,4,12,9,9,14,12,24,12,9,9,10,22,9,9,9,8,6,9,16,16,25,19,6,11,11,14,17,8,8,8,8,16,16,16,16,16,16,16,16,
                16,16,8,8,17,17,17,12,22,16,16,14,17,14,11,22,17,6,9,16,11,23,17,22,14,22,16,14,13,16,17,25,17,14,15,11,
                10,11,17,14,8,16,16,11,16,16,8,16,14,6,9,12,6,20,14,16,16,16,8,12,9,14,14,20,12,14,11,11,6,11,17,14,14,
                14,6,16,11,28,14,14,8,28,14,9,28,14,14,14,14,6,6,11,11,14,14,28,8,28,12,9,26,14,11,14 };

void BuildFont() {
    base = glGenLists(256);
    make_single_bitmap(&font_tex[1], "font.png", "aae.zip", 0);
    glBindTexture(GL_TEXTURE_2D, font_tex[1]);

    for (int i = 0; i < 256; ++i) {
        float cx = (float)(i % 16) / 16.0f;
        float cy = (float)(i / 16) / 16.0f;

        glNewList(base + i, GL_COMPILE);
        glBegin(GL_QUADS);
        glTexCoord2f(cx, 1.0f - cy - 0.0625f); glVertex2f(0.0f, 0.0f);
        glTexCoord2f(cx + 0.0625f, 1.0f - cy - 0.0625f); glVertex2f(1.0f, 0.0f);
        glTexCoord2f(cx + 0.0625f, 1.0f - cy); glVertex2f(1.0f, 1.0f);
        glTexCoord2f(cx, 1.0f - cy); glVertex2f(0.0f, 1.0f);
        glEnd();
        glTranslatef(fwidth[i], 0.0f, 0.0f);
        glEndList();
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

void KillFont() {
    glDeleteLists(base, 256);
    glDeleteTextures(1, &font_tex[1]);
}

void glPrint(float x, float y, int r, int g, int b, int alpha, float scale, float angle, int set, const char* fmt, ...) {
    char buffer[1024];
    va_list args;

    if (!fmt) return;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    glPushMatrix();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, font_tex[1]);

    glColor4ub((GLubyte)r, (GLubyte)g, (GLubyte)b, (GLubyte)alpha);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glTranslatef(x, y, 0.0f);
    glScalef(scale * 31.0f, scale * 31.0f, 1.0f);
    glRotatef(angle, 0.0f, 0.0f, 1.0f);
    glListBase(base - 32 + (128 * (set ? 1 : 0)));
    glCallLists((GLsizei)strlen(buffer), GL_UNSIGNED_BYTE, buffer);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub(255, 255, 255, 255);
    glDisable(GL_TEXTURE_2D);
    glPopMatrix();
}

void glPrint_centered(float y, const char* string, int r, int g, int b, int alpha, float scale, float angle, int set) {
    float total = get_string_pitch(string, scale, set);
    float x = 512.0f - total * 0.5f;
    glPrint(x, y, r, g, b, alpha, scale, angle, set, "%s", string);
}

float get_string_pitch(const char* string, float scale, int set) {
    float total = 0.0f;
    if (!string) return 0.0f;

    int offset = (set == 1) ? 128 : 0;
    for (size_t i = 0; string[i]; ++i) {
        int c = (unsigned char)string[i];
        if (c >= 32 && c < 256)
            total += fwidth[c - 32 + offset];
    }

    return total * scale;
}
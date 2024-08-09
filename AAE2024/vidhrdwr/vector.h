#ifndef VECTOR_H
#define VECTOR_H

typedef struct colorsarray { int r, g, b; } colors;
extern colors vec_colors[1024];

//void draw_points(void);
void draw_lines(void);
void draw_texs(void);
void draw_color_vectors();
void add_color_line(float sx, float sy, float ex, float ey, int r, int g, int b);
void add_color_point(float sx, float sy, int r, int g, int b);
void cache_line(float startx, float starty, float endx, float endy, int zvalue, float gc, float mod);
void cache_point(float pointx, float pointy, int zvalue, float gc, float mod, float adj);
void cache_txt(float pointx, float pointy, int size, int color);

void cache_end(void);
void cache_clear(void);

#endif

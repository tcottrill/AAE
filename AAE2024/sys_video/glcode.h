#ifndef GLCODE_H
#define GLCODE_H

#include <allegro.h>
#include "alleggl.h"

extern GLuint error_tex[1];
extern GLuint pause_tex[2];
extern GLuint fun_tex[4];
extern GLuint art_tex[8];
extern GLuint game_tex[10];
extern GLuint menu_tex[7];
extern GLuint font_tex[2];
extern GLuint fbotex;

#ifdef __cplusplus
extern "C" {
#endif

	void blit_any_tex(GLuint texture[], int blending, float alpha, int x, int y, int w, int h);
	int make_single_bitmap(GLuint* texture, const char* filename, const char* archname, int mtype);
	void blit_a_tex(GLuint texture[], int blending, int x, int y, int alpha, float scale);
	void blit_fs_tex(GLuint texture[], int blending, int zoom);
	void blit_fsa_texub(GLuint texture[], int blending, int r, int g, int b, int alpha, float zoom);
	void draw_center_tex(GLuint texture[], int size, int x, int y, int rotation, int facing, int r, int g, int b, int alpha, int blend);
	void draw_a_quad(int tleft, int bleft, int tright, int bright, int r, int g, int b, int alpha, int blend);
	void menu_textureit(GLuint texture[], int x, int y, int xsize, int ysize);
	void complete_render();
	void set_render();
	void fbo_init();
	void render();
	void myblending();
	void resize_art_textures();

	void final_render(int xmin, int xmax, int ymin, int ymax, int shiftx, int shifty);
	void draw2D(float x, float y);
	void final_render_blur(int xmin, int xmax, int ymin, int ymax, int shiftx, int shifty);
	void final_render_gauss(int xmin, int xmax, int ymin, int ymax, int shiftx, int shifty, float amount, float num);
	void blit_sized_texub(GLuint texture[], int blending, int clevel, int alpha, int xmin, int xmax, int ymin, int ymax, int shiftx, int shifty);
	void pause_loop();

	int init_gl(void);
	void end_gl();
	int init_shader();
	void WriteShaderError(GLhandleARB obj, const char* shaderName);
	void RendertoTarget();
	void RendertoTarget2();
	void FinalRenderB();
	void ping_pong();
	void set_blending2();
	void complete_fbo_render();
	//void textureit(float x, float y, int size);
	void show_error(void);
	void fadeit(void);
	void do_an_effect(int effect);
	void Widescreen_calc();
	//void run_gui(void);
	//void gui_loop(void);
	//void gui_input(int from, int to);
	//void show_info(void);
	//void gui_animate(int reset);
	//void shot_animate(int i);

	//void gl_reset(int xlow,int xhigh,int ylow,int yhigh, int blendmode);
	void free_game_textures();
	void texture_reinit();

	//int re_init_gl(void);
	void load_artwork(const struct artworks p[]);
	/*
	#ifdef GL_EXT_framebuffer_object
		  #ifdef _WIN32

			 #ifndef PFNGLGENFRAMEBUFFERSEXTPROC
				typedef void (APIENTRY * PFNGLGENFRAMEBUFFERSEXTPROC)
									 (GLsizei n, GLuint *renderbuffers);
			 #endif

			 #ifndef PFNGLFRAMEBUFFERTEXTURE2DEXTPROC
				typedef void (APIENTRY * PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)
									(GLenum target, GLenum attachment,
									 GLenum textarget, GLuint texture,
									 GLenum level);
			 #endif

			 #ifndef PFNGLFRAMEBUFFERTEXTURE3DEXTPROC
				typedef void (APIENTRY * PFNGLFRAMEBUFFERTEXTURE3DEXTPROC)
									(GLenum target, GLenum attachment,
									 GLenum textarget, GLuint texture,
									 GLenum level);
			 #endif

			 #ifndef PFNGLBINDFRAMEBUFFEREXTPROC
				typedef void (APIENTRY * PFNGLBINDFRAMEBUFFEREXTPROC)
									 (GLenum target, GLuint renderbuffer);
			 #endif

			 #ifndef PFNGLDELETERENDERBUFFERSEXTPROC
				typedef void (APIENTRY * PFNGLDELETERENDERBUFFERSEXTPROC)
									 (GLsizei n, const GLuint *renderbuffers);
			 #endif

			 #ifndef PFNGLGENERATEMIPMAPEXTPROC
				typedef void (APIENTRY * PFNGLGENERATEMIPMAPEXTPROC) (GLenum target);
			 #endif

			 PFNGLGENFRAMEBUFFERSEXTPROC glGenFramebuffersEXT;
			 PFNGLFRAMEBUFFERTEXTURE2DEXTPROC glFramebufferTexture2DEXT;
			 PFNGLFRAMEBUFFERTEXTURE3DEXTPROC glFramebufferTexture3DEXT;
			 PFNGLBINDFRAMEBUFFEREXTPROC glBindFramebufferEXT;
			 PFNGLDELETERENDERBUFFERSEXTPROC glDeleteRenderbuffersEXT;
			 PFNGLGENERATEMIPMAPEXTPROC glGenerateMipmapEXT;

		  #endif // _WIN32
	   #endif // GL_EXT_framebuffer_object

	*/

	void Any_Rect(int facing, int xmin, int xmax, int ymin, int ymax);

#ifdef __cplusplus
}
#endif

#endif
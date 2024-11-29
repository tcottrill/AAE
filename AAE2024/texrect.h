#pragma once

#ifndef TEXRect_H
#define TEXRect_H

#define USING_ALLEGRO 1

#ifdef USING_ALLEGRO 
#include "allegro.h"
#include "winalleg.h"
#include "alleggl.h"
#else
#include "glew.h"
#include "wglew.h"
#endif


class _Point2DA
{
	public:
		float x,y;
		float tx,ty;
		_Point2DA() :  x( 0 ), y( 0 ) , tx(0), ty(0) { }
		_Point2DA(float x, float y, float tx, float ty) : x(x), y(y), tx(tx), ty(ty) { }
};


class Rect2
{
	private:
		GLuint indices[4];
	
	
		int points;
		unsigned int color;
		int done;
		_Point2DA vertices[4];
		void SetVertex( int num, float x, float y , float tx, float ty );
  
	public:
	  Rect2 ();
	  ~Rect2 ();
 
		void TopLeft (float x, float y , float tx, float ty );
		void TopRight (float x, float y , float tx, float ty );
		void BottomLeft (float x, float y , float tx, float ty );
		void BottomRight (float x, float y , float tx, float ty );

		void TopLeft (float x, float y );
		void TopRight (float x, float y );
		void BottomLeft (float x, float );
		void BottomRight (float x, float );
		void GenArray();
		void Render(float scaley);
};

#endif
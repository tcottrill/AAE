/*-----------------------------------------------------------------------------

  texRect2.cpp

-------------------------------------------------------------------------------

 
-----------------------------------------------------------------------------*/
#include <string>
#include <sstream> 
#include <vector>
#include "texrect.h"
#include "log.h"

#include "colordefs.h"


Rect2::Rect2 ()
{
	color = MAKE_RGBA(255,255,255,255);
	done=0;
}

Rect2::~Rect2 ()
{
	
	wrlog("Array Deleted");
}


void Rect2::GenArray()
{
	indices[ 0 ] = 2;
    indices[ 1 ] = 1;
    indices[ 2 ] = 3;
    indices[ 3 ] = 0;
	
   
	wrlog("Array created");
	done=1;

}

void Rect2::SetVertex( int num, float x, float y , float tx, float ty )
{
	vertices[num].x = x;
	vertices[num].y= y;
	vertices[num].tx = tx;
	vertices[num].ty = ty;
 }


void Rect2::BottomLeft(float x, float y , float tx, float ty )
{
	SetVertex( 0, x, y , tx, ty );
}

void Rect2::TopLeft(float x, float y , float tx, float ty )
{
	SetVertex( 1, x, y , tx, ty );
}

void Rect2::TopRight(float x, float y , float tx, float ty )
{
	SetVertex( 2, x, y , tx, ty );
}

void Rect2::BottomRight(float x, float y , float tx, float ty )
{
	SetVertex( 3, x, y , tx, ty );
}

//////////////////////////////////////////////////////////////////////
void Rect2::BottomLeft (float x, float y )
{
	 SetVertex( 0, x, y , 0.0f, 0.0f );
}

void Rect2::TopLeft (float x, float y )
{
	SetVertex( 1, x, y , 0.0f, 1.0f );
}

void Rect2::TopRight (float x, float y )
{
	 SetVertex( 2, x, y , 1.0f, 1.0f );
}

void Rect2::BottomRight (float x, float y )
{
	 SetVertex( 3, x, y , 1.0f, 0.0f );
	
}


void Rect2::Render (float scaley)
{
	// I feel like I am going backwards here, but this will get the job done. 

	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(vertices[0].x, vertices[0].y);
	glTexCoord2f(0, 1); glVertex2f(vertices[1].x, vertices[1].y * scaley);
	glTexCoord2f(1, 1); glVertex2f(vertices[2].x, vertices[2].y * scaley);
	glTexCoord2f(1, 0); glVertex2f(vertices[3].x, vertices[3].y);
	glEnd();


}



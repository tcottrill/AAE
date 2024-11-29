/*-----------------------------------------------------------------------------

  texRect2DVBO.cpp

-------------------------------------------------------------------------------

 
-----------------------------------------------------------------------------*/
#include <string>
#include <sstream> 
#include <vector>
#include "texRect2Dvbo.h"
#include "log.h"


#include "colordefs.h"

#define BUFFER_OFFSET(i) ((void*)(i))

//#define BUFFER_OFFSET(i) ((char*)NULL + (i))

#define MEMBER_OFFSET(s,m) ((char *)NULL + (offsetof(s,m)))

Rect2DVBO::Rect2DVBO ()
{
	gVBO = NULL;
	gIBO = NULL;
	color = MAKE_RGBA(255,255,255,255);
	done=0;
}

Rect2DVBO::~Rect2DVBO ()
{
	glDeleteBuffers(1, &gVBO);
	glDeleteBuffers(1, &gIBO);
	wrlog("Array Deleted");
}


void Rect2DVBO::GenArray()
{
	indices[ 0 ] = 2;
    indices[ 1 ] = 1;
    indices[ 2 ] = 3;
    indices[ 3 ] = 0;
	
    //Create VBO
    glGenBuffers( 1, &gVBO );
    glBindBuffer( GL_ARRAY_BUFFER, gVBO );
    glBufferData( GL_ARRAY_BUFFER, sizeof(_Point2DA) * 4, &vertices[0].x, GL_STATIC_DRAW );
	//glBindBuffer(GL_ARRAY_BUFFER, 0);

    //Create IBO
    glGenBuffers( 1, &gIBO );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, gIBO );
    glBufferData( GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint)*4 , indices, GL_STATIC_DRAW );
	//glBindBuffer( GL_ELEMENT_ARRAY_BUFFER,0);

	wrlog("Array created");
	done=1;

}

void Rect2DVBO::SetVertex( int num, float x, float y , float tx, float ty )
{
	vertices[num].x = x;
	vertices[num].y= y;
	vertices[num].tx = tx;
	vertices[num].ty = ty;
 }


void Rect2DVBO::BottomLeft(float x, float y , float tx, float ty )
{
	SetVertex( 0, x, y , tx, ty );
}

void Rect2DVBO::TopLeft(float x, float y , float tx, float ty )
{
	SetVertex( 1, x, y , tx, ty );
}

void Rect2DVBO::TopRight(float x, float y , float tx, float ty )
{
	SetVertex( 2, x, y , tx, ty );
}

void Rect2DVBO::BottomRight(float x, float y , float tx, float ty )
{
	SetVertex( 3, x, y , tx, ty );
}

//////////////////////////////////////////////////////////////////////
void Rect2DVBO::BottomLeft (float x, float y )
{
	 SetVertex( 0, x, y , 0.0f, 0.0f );
}

void Rect2DVBO::TopLeft (float x, float y )
{
	SetVertex( 1, x, y , 0.0f, 1.0f );
}

void Rect2DVBO::TopRight (float x, float y )
{
	 SetVertex( 2, x, y , 1.0f, 1.0f );
}

void Rect2DVBO::BottomRight (float x, float y )
{
	 SetVertex( 3, x, y , 1.0f, 0.0f );
	
}


void Rect2DVBO::Render ()
{
	
	if (done==0) GenArray();
	//Enable vertex arrays
 
    //Set vertex data
	glEnableClientState( GL_VERTEX_ARRAY );
	glBindBuffer( GL_ARRAY_BUFFER, gVBO );
	glVertexPointer( 2, GL_FLOAT,  sizeof(_Point2DA),  BUFFER_OFFSET(0));

	//Set texture coordinate data
	glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer( 2, GL_FLOAT,  sizeof(_Point2DA),  BUFFER_OFFSET(8));//(void*)offsetof(_Point2DA, texs) ); 

	// Set Color Data
	//	glEnableClientState(GL_COLOR_ARRAY);
    // glColorPointer (4, GL_UNSIGNED_BYTE , sizeof(_Point2DA) , &vertices[0].r );//&vertices[0].r); BUFFER_OFFSET(4*sizeof(float))
	
	//Draw quad using vertex data and index data
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, gIBO );
    glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_INT, 0);

	int err = glGetError();
    if(err != 0) 
	  {
		wrlog("openglerror %d",err);
		exit(1);
	 }
			
	glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_VERTEX_ARRAY );

	//Disable vertex arrays
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER,0);

}



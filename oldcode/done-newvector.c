#include "newvector.h"
#include "glcode.h"
#include "globals.h"

extern GLuint game_tex[10];

//#define CLIP(x) (x)<0?0:((x)>255?255:(x)); 
#define CLIP(v) ((v) <= 0 ? 0 : (v) >= 255 ? 255 : (v))

#define MAX_CACHE  6

//TEMP
float linewidth=2.0;
double pointsize=1.6;

static int vp=0;
static int pp=0;
static int tp=0;

static int lastvp=0;
static int lastpp=0;
static int lasttp=0;

static int cache_count=0;

typedef struct 
{

 int x, y;        //Vertex
 int r, g, b, a;   //Color	 This was BYTE but I had to make it an int to support gain!!
 int size;
 
} MyTex;


typedef struct 
  {

   int x, y;        //Vertex
   int r, g, b, a; //color This was BYTE but I had to make it an int to support gain!!	

  } MyVector;
     
 
   MyVector vertex[100000];
   MyVector points[100000];
   MyTex tex[100000];


void Set_Vector_Blendmode(int mode)
{

}


void Add_TexPoint(int x, int y, int shotsize, int color)
{
       tex[tp].x = x;
       tex[tp].y = y;
	   tex[tp].r = RGB_RED(color);
       tex[tp].g = RGB_GREEN(color);
       tex[tp].b = RGB_BLUE(color);
       tex[tp].a = RGB_ALPHA(color);
       tex[tp].size = shotsize;
       tp++;
}


void Add_Line(int sx, int sy, int ex, int ey, int color)
{
       vertex[vp].x = sx;
       vertex[vp].y = sy;
	   vertex[vp].r = RGB_RED(color);
       vertex[vp].g = RGB_GREEN(color);
       vertex[vp].b = RGB_BLUE(color);
       vertex[vp].a = RGB_ALPHA(color);

       vp++;
       vertex[vp].x = ex;
       vertex[vp].y = ey;
	   vertex[vp].r = RGB_RED(color);
       vertex[vp].g = RGB_GREEN(color);
       vertex[vp].b = RGB_BLUE(color);
       vertex[vp].a = RGB_ALPHA(color);
       vp++;
}


void Add_Point(int x, int y, int color)
{
       points[pp].x=x;
       points[pp].y=y;
	   points[pp].r= RGB_RED(color);
       points[pp].g= RGB_GREEN(color);
       points[pp].b= RGB_BLUE(color);
       points[pp].a= RGB_ALPHA(color);
       pp++;
}

void Cache_Draw()
{
 //Do not Exceed MAX_CACHE number of cached frame draws in a row.
  cache_count++;
 log_it("Cache Count %d",cache_count);
 if (cache_count > MAX_CACHE) {vp=0;pp=0;tp=0;lastvp=0;lasttp=0;lastpp=0;cache_count=0; log_it("MAX Cache Reached!");return; }

 //log_it("Cache Draw!");
 vp=lastvp;
 pp=lastpp;
 tp=lasttp;
}

void Reset_Vector_Draw()
{
 lastvp=vp;vp=0;
 lastpp=pp;pp=0;
 lasttp=tp;tp=0;
 //cache_count=0;
}

static void Render_Shots() 
{
	int i;

	glEnable(GL_TEXTURE_2D);
	glBlendFunc(GL_ONE, GL_ONE);
	glBindTexture(GL_TEXTURE_2D, game_tex[0]);  

	for (i=0; i < tp; i++){  
    glColor4ub( tex[i].r,tex[i].g,tex[i].b,tex[i].a );
	glBegin(GL_QUADS);
	  glTexCoord2i(1,0);glVertex2i(tex[i].x+tex[i].size, tex[i].y+tex[i].size);          
	  glTexCoord2i(0,0);glVertex2i(tex[i].x-tex[i].size, tex[i].y+tex[i].size);
	  glTexCoord2i(0,1);glVertex2i(tex[i].x-tex[i].size, tex[i].y-tex[i].size);
	  glTexCoord2i(1,1);glVertex2i(tex[i].x+tex[i].size, tex[i].y-tex[i].size);
	  glEnd();
	}
	glDisable(GL_TEXTURE_2D);	
} 


static void Set_Vector_Drawmode()
{
   glDisable(GL_DITHER);
   glDisable(GL_TEXTURE_2D);	
   //glEnable(GL_BLEND);
  // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  // glEnable(GL_LINE_SMOOTH);
  // glEnable(GL_POINT_SMOOTH);
   glLineWidth((float) config.linewidth);
   glPointSize((float) config.pointsize);
}


void Render_Vectors()
{
  
  int i=0;
  int gain=50;

  Set_Vector_Drawmode();
   
	//Lines are drawn in revers order to correct drawing issues in Gravitar. May introduce issues in other games?
   if (vp==0) Cache_Draw(); else cache_count=0;
	if (vp)
	{         
			  glBegin(GL_LINES);
			  for (i=vp-2; i > -2; i-=2)
			   {
                  if (gain)
					{   
						vertex[i].r +=gain;	
						vertex[i].g +=gain;		
						vertex[i].b +=gain;
						vertex[i].a +=gain;
                       
						vertex[i].r = CLIP(vertex[i].r);
						vertex[i].g = CLIP(vertex[i].g);
						vertex[i].b = CLIP(vertex[i].b);
						vertex[i].a = CLIP(vertex[i].a);	
							
					}
                  glColor4ub(vertex[i].r,vertex[i].g,vertex[i].b,vertex[i].a);
				  glVertex2i(vertex[i].x,vertex[i].y);
				  glVertex2i(vertex[i+1].x,vertex[i+1].y);
			   }
			  glEnd();      

	}


  if (pp)
	{        
			  glBegin(GL_POINTS);
			  for (i=pp-1; i > -1; i-=1)
			  {
                if (gain)
					{
						points[i].r +=gain;	
						points[i].g +=gain;		
						points[i].b +=gain;
                        points[i].a +=gain;

						points[i].r = CLIP(points[i].r);
						points[i].g = CLIP(points[i].g);
						points[i].b = CLIP(points[i].b);
						points[i].a = CLIP(points[i].a);		
					}
				glColor4ub(points[i].r,points[i].g,points[i].b,points[i].a);
				glVertex2i(points[i].x,points[i].y);
			  }
			  glEnd();      
	}


 if (tp){Render_Shots();}
 Reset_Vector_Draw();

}
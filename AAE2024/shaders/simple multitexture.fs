const char *texfragText = STRINGIFY(
uniform sampler2D mytex1;
uniform sampler2D mytex2;
uniform sampler2D mytex3;
uniform sampler2D mytex4;

uniform int useart;
uniform int usefb;
uniform int useglow;
uniform float glowamt; //Amount of glow texture to combine

varying vec4 result;

void main (void)
{
   
   vec4 texval1 = texture2D(mytex1, gl_TexCoord[0].xy); //Artwork
   vec4 texval2 = texture2D(mytex2, gl_TexCoord[0].xy); //Vectors
   vec4 texval3 = texture2D(mytex3, gl_TexCoord[0].xy); //Glow
   vec4 texval4 = texture2D(mytex4, gl_TexCoord[0].xy); //Feedback
   
   result= texval2 * 1.8;
   if (useart > 0)  result += texval1 * 0.65;
   //if (useglow > 0) result += texval3 * glowamt;
   //if (usefb > 0)   result += texval4;    
  
   gl_FragColor = result;
}

);
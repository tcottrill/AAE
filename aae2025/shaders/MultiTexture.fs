const char *texfragText = STRINGIFY(

uniform int useart;
uniform int usefb;
uniform int useglow;
uniform float glowamt; //Amount of glow texture to combine
uniform int brighten;

uniform sampler2D mytex1;
uniform sampler2D mytex2;
uniform sampler2D mytex3;
uniform sampler2D mytex4;

//varying vec4 result;


void main (void)
{
   float bval=1.0;
   
   //Init textures
   vec4 texval1 = texture2D(mytex1, gl_TexCoord[0].xy ); //Artwork
   vec4 texval2 = texture2D(mytex2, gl_TexCoord[0].xy ); //Vectors
   vec4 texval3 = texture2D(mytex3, gl_TexCoord[0].xy); //Glow
   vec4 texval4 = texture2D(mytex4, gl_TexCoord[0].xy); //Feedback
   //Combine
   
   
   if ( brighten > 0 && useart > 0 && usefb ==0) bval = 1.6;
   else if (usefb > 0) bval=1.0;
   vec4 result = texval2 * bval;
   
   if (useart > 0)  result += (texval1 * .60);
   if (useglow > 0) result +=  (texval3 * glowamt);
   if (usefb > 0) result += (texval4 * 0.25); 
    //Finalize
   gl_FragColor = result;
}
);
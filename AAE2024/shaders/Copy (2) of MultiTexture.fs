const char *texfragText = STRINGIFY(

uniform sampler2D mytex1;
uniform sampler2D mytex2;
//varying vec2 Tex_coord;
vec4 value1;
vec4 value2;

void main()
{
   //gl_FragColor = texture2D(mytex1, Tex_coord) + texture2D(mytex2, Tex_coord);
   //gl_FragColor = texture2D(mytex1, Tex_coord);
   value1 = texture2D(mytex1, Tex_coord[0].st);
   value2 = texture2D(mytex2, Tex_coord[1].st);
   gl_FragColor = value1 + value2;
}
);
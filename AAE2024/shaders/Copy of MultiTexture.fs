const char *texfragText = STRINGIFY(

uniform sampler2D mytex1;
uniform sampler2D mytex2;
varying vec2 Tex_coord;

void main()
{
  gl_FragColor = texture2D(mytex1, Tex_coord) + texture2D(mytex2, Tex_coord);
}
);
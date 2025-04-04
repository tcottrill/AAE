const char *motionfs = STRINGIFY(


uniform sampler2D TexUnit0;
uniform int blendval;

void main()
{
    vec4 color = gl_Color;
         
    gl_FragColor = color;
}

);
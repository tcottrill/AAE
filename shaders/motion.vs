const char *motionvs = STRINGIFY(


void main()
{
	// Transforming The Vertex
     gl_TexCoord[0] = gl_MultiTexCoord0;
     gl_Position    = ftransform();


}

);

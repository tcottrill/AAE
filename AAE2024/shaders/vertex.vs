const char *vertText = STRINGIFY(


void main()
{
	// Transforming The Vertex
     gl_TexCoord[0] = gl_MultiTexCoord0;
  // gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
     gl_Position    = ftransform();


}

);

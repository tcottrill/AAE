/*==================
multitexture.fp
Written 2006.03.03 for 15-462 by Chris Cameron

This program will take two textures and blend them together using
the vertex attribute blend as the weight for blending.  

This program will read the two textures at varying texture 
coordinate zero and will weight the earth picture by v_blend
and the mars picture by 1-v_blend
==================*/

/* this will be set to the blend parameter of each vertex, and
will be interpolated smoothly over the polygons */
varying float v_blend;

/* the first texture to blend between */
uniform sampler2D earthTexture;

/* the second texture to blend between */
uniform sampler2D marsTexture;

void main ()
{
	/* sample earth color */
	vec4 earthColor = texture2D (earthTexture, gl_TexCoord[0].xy);

	/* sample mars color */
	vec4 marsColor = texture2D (marsTexture, gl_TexCoord[0].xy);

	/* set output to be the blend between them */
	gl_FragColor = v_blend * earthColor + (1.0 - v_blend) * marsColor;
}


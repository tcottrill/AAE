#include "texture_loading_shim.h"
#include "../fileio/aae_fileio.h"
#include "../log.h"
//For texture loading
#define STB_IMAGE_IMPLEMENTATION
//#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
//#include "stb_image_write.h"

GLuint load_texture(const char* filename, const char* archname, int numcomponents, int filter)
{
	GLuint tex = 0;
	int width = 0;
	int height = 0;
	int comp = 0;
	unsigned char* image_data = 0;
	unsigned char* raw_data = 0;

	//Stupid
	stbi_set_flip_vertically_on_load(1);

	//If no archive name is supplied, it's assumed the file is being loaded from the file system.
	if (!archname)
	{
		image_data = stbi_load(filename, &width, &height, &comp, STBI_rgb_alpha);
	}
	else
	{
		//Otherwise load with our generic zip functionality
		raw_data = load_zip_file(filename, archname);
		image_data = stbi_load_from_memory(raw_data, (int)(get_last_zip_file_size()), &width, &height, &comp, STBI_rgb_alpha);
	}
	if (!image_data) {
		wrlog("ERROR: could not load %s\n", filename);
		//	return 0;
	}
	wrlog("Texture x is %d, y is %d, components %d", width, height, comp);
	// NPOT check, we checked the card capabilities beforehand, flag if it's not npot

	if ((width & (width - 1)) != 0 || (height & (height - 1)) != 0) {
		wrlog("WARNING: texture %s is not power-of-2 dimensions\n", filename);
	}

	//Create Texture

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	// OLD OpenGL 2.0 style binding
	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGBA8,
		width,
		height,
		0,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		image_data
	);

	//glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
	//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	//Check for errors
	//check_gl_error();

	//The below gluBuild2DMipmaps is ONLY for maximum compatibility using OpenGL 1.4 - 2.0.
	//In a real program, you should be using GLEW or some other library to allow glGenerateMipmap to work.
	// it's much better,and you should be using an OpenGL 3+ context anyways. This is just for my example program.
	//gluBuild2DMipmaps(GL_TEXTURE_2D, 4, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image_data); //Never use this in anything but an opengl 2 demo.
	//This is openGL3 now so:
	//glGenerateMipmap(GL_TEXTURE_2D); //Only if using opengl 3+, next example...

	//Now that we're done making a texture, free the image data
	stbi_image_free(image_data);
	free(raw_data);
	//set image data properties

	wrlog("New Texture created:");
	wrlog("Texture ID: %d", tex);
	return tex;
}
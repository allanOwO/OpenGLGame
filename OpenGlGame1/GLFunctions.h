#pragma once
#include <glad/glad.h>

// OpenGL functions newer than the 3.3 that Include/glad was generated for
// Loaded by hand after the context is created (see Main::init)
// If more 4.x functions are needed, regenerating glad for gl=4.3 would replace this file

#ifndef GL_DRAW_INDIRECT_BUFFER
#define GL_DRAW_INDIRECT_BUFFER 0x8F3F
#endif

typedef void (APIENTRYP PFN_glMultiDrawElementsIndirect)(GLenum mode, GLenum type, const void* indirect, GLsizei drawcount, GLsizei stride);

namespace GLFunctions {
	// Many indexed draws described by commands in the GL_DRAW_INDIRECT_BUFFER, in one call (GL 4.3)
	inline PFN_glMultiDrawElementsIndirect multiDrawElementsIndirect = nullptr;

	// false if the driver doesn't provide them
	inline bool load(GLADloadproc getProcAddress) {
		multiDrawElementsIndirect = reinterpret_cast<PFN_glMultiDrawElementsIndirect>(getProcAddress("glMultiDrawElementsIndirect"));
		return multiDrawElementsIndirect != nullptr;
	}
}

#include "shadowAtlas.h"
#include "fbo.h"
#include "texture.h"
#include "shader.h"


shadowAtlas::shadowAtlas()
{
	atlasFBO = new FBO();
	atlasFBO->setDepthOnly(4096,4096);
	atlasTexture = new Texture();
}

shadowAtlas::~shadowAtlas()
{
	delete this->atlasTexture;
	delete this->atlasFBO;
	this->cellData.clear();
}

void shadowAtlas::clearAtlas()
{
	this->atlasFBO->bind();
	
	this->atlasFBO->unbind()
}

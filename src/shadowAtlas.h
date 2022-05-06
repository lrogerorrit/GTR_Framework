#pragma once
#include "includes.h"
#include "framework.h"


class FBO;
class Texture;


class shadowAtlas
{
private:
	FBO* atlasFBO;
	Texture* atlasTexture;

	std::vector<Vector4> cellData;
public:
	shadowAtlas(int size);
	~shadowAtlas();

	
};


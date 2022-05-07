#pragma once
#include "includes.h"
#include "framework.h"


class FBO;
class Texture;
class Shader;

class shadowAtlas
{
private:
	FBO* atlasFBO;
	Texture* atlasTexture;

	std::vector<Vector4> cellData;
public:
	shadowAtlas();
	~shadowAtlas();

	void clearAtlas();
	
	void clearCell(std::string cellName);

	int addCell(std::string cellName, Vector4 cellData);
	void removeCell(std::string cellName);
	
	void uploadDataToShader(Shader* shader, const char* uniformName);
	
	
	
};


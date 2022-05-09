#pragma once
#include "includes.h"
#include "framework.h"
#include <string>
#include "material.h"
#include "camera.h"


#define MAX_ATLAS_LIGHTS 7;

class FBO;
class Shader;


namespace GTR {
	class LightEntity;
	class RenderCall;
}
struct shadowData {
	
	GTR::LightEntity* light;
	int shadowDimensions;
	Vector2 pos;
};



namespace GTR{

	class shadowAtlas
	{
	private:
		const int textureSize = 4096;
		const int maxLights =MAX_ATLAS_LIGHTS;
		
		std::vector<shadowData> dataArray;
		

		int lightNum = 0;

		inline Vector2 getTilePosition(int index);
		inline int getTileSize(int index);

	public:
		FBO* atlasFBO;
		
		shadowAtlas();
		
		~shadowAtlas();

		void clearArray();


		void addLight(LightEntity* light);
		
		shadowData getData(int index);
		shadowData getData(GTR::LightEntity* light);

		void calculateShadows(std::vector<RenderCall>& renderCalls);
		//void uploadDataToShader(Shader* shader);

		void uploadDataToShader(Shader* shader, std::vector<LightEntity*>& lights);

		void displayDepthToViewport(int size);
		
		
		
		

		
		
		
		
		




	};
};


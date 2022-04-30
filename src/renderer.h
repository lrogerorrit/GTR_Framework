#pragma once
#include "prefab.h"

//forward declarations
class Camera;

namespace GTR {

	enum class eMultiLightType {
		SINGLE_PASS,
		MULTI_PASS,
	};

	class Prefab;
	class Material;
	
	// This class is in charge of rendering anything in our system.
	// Separating the render from anything else makes the code cleaner

	class RenderCall {
	public:
		Mesh* mesh;
		Material* material;
		Matrix44 model;
		
		float distance_to_camera;
	};

	class Renderer
	{
	private:
		std::vector<RenderCall> render_calls;
		std::vector<GTR::LightEntity*> lights;

		
		
	public:
		bool orderNodes = true;
		int multiLightType = (int) eMultiLightType::SINGLE_PASS;
		

		//add here your functions
		//...

		//renders several elements of the scene
		void renderScene(GTR::Scene* scene, Camera* camera);
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		void renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		void generateShadowMaps(LightEntity* light);
	};

	Texture* CubemapFromHDRE(const char* filename);

};
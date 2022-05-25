#pragma once
#include "prefab.h"

//forward declarations
class Camera;

namespace GTR {
	class shadowAtlas;

	enum class eMultiLightType {
		SINGLE_PASS,
		MULTI_PASS,
	};

	enum class ePipeLineType {
		FORWARD,
		DEFERRED
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
		BoundingBox boundingBox;
		float distance_to_camera;
	};

	class Renderer
	{
	private:
		std::vector<RenderCall> render_calls;
		std::vector<GTR::LightEntity*> lights;

		std::vector<Vector3> randomPoints;
		
		
	public:
		GTR::shadowAtlas* shadowMapAtlas;
		bool orderNodes = true;
		bool useOcclusion = true;
		bool useNormalMap = true;
		bool useEmissive = true;
		bool useSSAO = false;
		bool useSSAOBlur = false;

		bool showAtlas = false;
		bool showGBuffers = false;
		bool showSSAO = true;
		bool isOptimizedDeferred = true;
		
		
		int multiLightType = (int) eMultiLightType::SINGLE_PASS;
		ePipeLineType pipelineType = ePipeLineType::FORWARD;

		FBO* gbuffers_fbo= NULL;
		FBO* illumination_fbo= NULL;
		FBO* ssao_fbo= NULL;
		
		
		
		
		

		//add here your functions
		//...

		Renderer();

		//renders several elements of the scene
		void renderScene(GTR::Scene* scene, Camera* camera);

		void RenderForward(Camera* camera, GTR::Scene* scene);
		void renderSSAO(Camera* cam, GTR::Scene* scene, Matrix44& invVP, Mesh* quad);
		void RenderDeferred(Camera* camera, GTR::Scene* scene);
		
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);

		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterialToGBuffers(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
		void uploadSingleLightToShader(Shader* shader, GTR::LightEntity* light);
		void renderMeshWithMaterialAndLighting(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		

		
	};
	std::vector<Vector3> generateSpherePoints(int num, float radius, bool hemi);
		

	Texture* CubemapFromHDRE(const char* filename);

};
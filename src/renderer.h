#pragma once
#include "prefab.h"
#include "sphericalharmonics.h"


//forward declarations
class Camera;


enum class LUTTypes {
	BleachBypass,
	Bleak,
	CandleLight,
	FoggyNight,
	Horror,
	LateNight,
	LeaveBlue,
	LeaveGreen,
	LeaveRed,
	Sunset,
	TealOrange,
	TealOrangeContrast,
	TealOrangeLowContrast,
	Vintage	
};


struct sProbe {
	Vector3 pos; //where is located
	Vector3 local; //its ijk pos in the matrix
	int index; //its index in the linear array
	SphericalHarmonics sh; //coeffs
};

namespace GTR {
	class shadowAtlas;
	class ReflectionProbeEntity;

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
		std::vector<GTR::DecalEntity*> decals;

		std::vector<Vector3> randomPoints;
		std::vector<sProbe> irrProbes;
		std::vector<Texture*> LUTTextures;
		
		
		
	public:
		GTR::shadowAtlas* shadowMapAtlas;
		bool orderNodes = true;
		bool useOcclusion = true;
		bool useNormalMap = true;
		bool useEmissive = true;
		bool useSSAO = true;
		bool useSSAOBlur = false;
		bool useHDR = false;
		bool useTonemapper = false;
		bool usePBR = true;
		bool useIrr = false;
		bool useReflections = true;
		bool useVolumetric = true;

		
		
		bool useDoF = false;
		bool useBloom = false;
		bool useFXAA = false;
		bool useLUT = false;
		bool useGrain = false;
		bool useLensDistortion = false;
		bool useChromaticAberration = false;
		bool useMotionBlur = false;

		bool showAtlas = false;
		bool showGBuffers = false;
		bool showSSAO = false;
		bool isOptimizedDeferred = true;
		bool displayIRRProbes = false;
		bool displayReflectionProbes = false;

		bool shouldCalculateProbes = false;

		bool isRenderingReflections = false;
		
		float u_scale = 1.0f;
		float u_average_lum = 1.0f;
		float u_lumwhite2 = 1.0f;
		
		float airDensity = 1;
		
		float DoF_minDist = 1;
		float DoF_maxDist = 10;
		Vector2 DoF_focusPoint;

		LUTTypes Luttype= LUTTypes::BleachBypass;
		float LUTmix = .5;

		float lens_barrel_power=1.5;

		float velocityFactor = .4;
		Matrix44 prevViewProjection;
		Vector3 prevCamPos;
		

		float bright_color_threshold = .7;
		
		

		ReflectionProbeEntity* probe=NULL;
		
		Mesh* cube;
		
		int multiLightType = (int) eMultiLightType::SINGLE_PASS;
		ePipeLineType pipelineType = ePipeLineType::FORWARD;

		FBO* gbuffers_fbo= NULL;
		FBO* decals_fbo= NULL;
		FBO* illumination_fbo= NULL;
		FBO* ssao_fbo= NULL;
		FBO* tonemapper_fbo= NULL;
		FBO* deferred_alpha_fbo = NULL; //TODO: Remove
		FBO* irradiance_fbo = NULL;
		FBO* reflection_fbo = NULL;
		FBO* volumetric_fbo = NULL;
		FBO* DoF_fbo = NULL;
		FBO* FBX = NULL;
		
		Texture* irr_probe_texture = NULL;
		Texture* skybox = NULL;
		
		Texture* postFX_textureA;
		Texture* postFX_textureB;
		Texture* postFX_textureC;
		
		Vector3 irr_probe_dim;
		Vector3 start_irr;
		Vector3 end_irr;
		

		float irrMultiplier = 1.0;
		
		
		

		//add here your functions
		//...

		void loadLUTTextures();
		Texture* getLUTTexture(LUTTypes type);

		Renderer();

		void renderSkybox(Camera* camera);

		//renders several elements of the scene
		void renderScene(GTR::Scene* scene, Camera* camera);

		void RenderForward(Camera* camera, GTR::Scene* scene);
		void renderSSAO(Camera* cam, GTR::Scene* scene, Matrix44& invVP, Mesh* quad);
		void RenderDeferred(Camera* camera, GTR::Scene* scene);
		
		bool applyFX(Camera* camera, Texture* color_texture, Texture* depth_texture);
		
	
		//to render a whole prefab (with all its nodes)
		void renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera);

		//to render one node from the prefab and its children
		void renderNode(const Matrix44& model, GTR::Node* node, Camera* camera);


		//to render one mesh given its material and transformation matrix
		void renderMeshWithMaterialToGBuffers(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);
		void uploadSingleLightToShader(Shader* shader, GTR::LightEntity* light);

		void updateReflectionProbes(GTR::Scene* scene);

		void renderReflectionProbes(GTR::Scene* scene, Camera* cam);

		void captureReflectionProbe(GTR::Scene* scene, Texture* tex, Vector3 pos);
		
		void renderMeshWithMaterialAndLighting(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera);

		void renderProbe(Vector3 pos, float size, float* coeffs);

		void CalculateProbe(sProbe& probe, Camera* cam, Scene* scene);

		void CreateIrradianceGrid();

		void CalculateAllProbes(Scene* scene);

		void StoreProbesToTexture();

		void CalculateIrradianceProbes(Scene* scene);

		

		
		

		

		
	};
	std::vector<Vector3> generateSpherePoints(int num, float radius, bool hemi);
		

	Texture* CubemapFromHDRE(const char* filename);

};
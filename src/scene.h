#ifndef SCENE_H
#define SCENE_H

#include "framework.h"
#include "camera.h"
#include <string>
#include "fbo.h"

//forward declaration
class cJSON; 


//our namespace
namespace GTR {



	enum eEntityType {
		NONE = 0,
		PREFAB = 1,
		LIGHT = 2,
		CAMERA = 3,
		REFLECTION_PROBE = 4,
		DECALL = 5
	};
	
	enum class eLightType {
		POINT,
		SPOT,
		DIRECTIONAL,
	
	};

	class Scene;
	class Prefab;

	//represents one element of the scene (could be lights, prefabs, cameras, etc)
	class BaseEntity
	{
	public:
		Scene* scene;
		std::string name;
		eEntityType entity_type;
		Matrix44 model;
		bool visible;
		BaseEntity() { entity_type = NONE; visible = true; }
		virtual ~BaseEntity() {}
		virtual void renderInMenu();
		virtual void configure(cJSON* json) {}
	};

	class LightEntity : public GTR::BaseEntity
	{
	public:
		Vector3 color;
		Vector3 target;
		float intensity;
		eLightType light_type;
		float max_distance;
		float cone_angle;
		float cone_exp;
		float area_size;
		float shadow_bias;
		bool cast_shadows;
		Vector3 lightDirection;
		Camera* shadow_cam= NULL;
		FBO* shadow_fbo= NULL; //todo refactor and remove
		Texture* shadow_map= NULL; //todo refactor and remove
		bool showSM = false; //todo refactor and remove

		bool has_shadow_map=false;
		int shadowAtlasIndex = -1;
		
		
		
		
		LightEntity();
		virtual void renderInMenu();
		virtual void configure(cJSON* json);
		
		
	};

	//represents one prefab in the scene
	class PrefabEntity : public GTR::BaseEntity
	{
	public:
		std::string filename;
		Prefab* prefab;
		
		PrefabEntity();
		virtual void renderInMenu();
		virtual void configure(cJSON* json);
	};

	//contains all entities of the scene
	class Scene
	{
	public:
		static Scene* instance;

		Vector3 background_color;
		Vector3 ambient_light;
		Camera main_camera;
		

		Scene();

		std::string filename;
		std::vector<BaseEntity*> entities;

		void clear();
		void addEntity(BaseEntity* entity);

		bool load(const char* filename);
		BaseEntity* createEntity(std::string type);
	};

};

#endif
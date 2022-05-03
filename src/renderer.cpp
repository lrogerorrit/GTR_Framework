#include "renderer.h"

#include "camera.h"
#include "shader.h"
#include "mesh.h"
#include "texture.h"
#include "prefab.h"
#include "material.h"
#include "utils.h"
#include "scene.h"
#include "extra/hdre.h"
#include "framework.h"
#include <algorithm>
#include <string>


using namespace GTR;

//function to calculate distance between two vector3;
float distance(Vector3 a, Vector3 b)
{
	return sqrt(pow(a.x - b.x, 2) + pow(a.y - b.y, 2) + pow(a.z - b.z, 2));
}


//Note to self, this is for < operator
bool transparencySort(const GTR::RenderCall& a, const GTR::RenderCall& b) {
	if (b.material->alpha_mode > 0 && a.material->alpha_mode > 0) //if both transparent order from far to near
		return a.distance_to_camera > b.distance_to_camera;
	else if (b.material->alpha_mode == 0 && a.material->alpha_mode == 0) //if both opaque order from near to far
		return a.distance_to_camera < b.distance_to_camera;
	else
		return a.material->alpha_mode == 0; //else put opaque before transparent
}

bool lightSort(const GTR::LightEntity* a, const GTR::LightEntity* b) {
	
	if (a->cast_shadows && b->cast_shadows)
		return (int)a->light_type <= (int) b->light_type;
	else
		return (int)a->cast_shadows >= (int)b->cast_shadows ;
}
	



void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);

	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	checkGLErrors();

	//render entities

	this->render_calls.clear();
	this->lights.clear();
	
	for (int i = 0; i < scene->entities.size(); ++i)
	{
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible)
			continue;

		//is a prefab!
		if (ent->entity_type == PREFAB)
		{
			PrefabEntity* pent = (GTR::PrefabEntity*)ent;
			if (pent->prefab) {
				renderPrefab(ent->model, pent->prefab, camera);
				
			}
		}
		//is a light!
		else if (ent->entity_type == eEntityType::LIGHT) {
			LightEntity* light = (GTR::LightEntity*)ent;
			this->lights.push_back(light);
		}
	}

	

	if (this->orderNodes)
		std::sort(this->render_calls.begin(), this->render_calls.end(), transparencySort);
	

	//generate shadowmaps
	for (int i = 0; i < lights.size(); ++i) {
		LightEntity* light = lights[i];
		if (light->cast_shadows)
			generateShadowMaps(light);
	}
	std::sort(this->lights.begin(), this->lights.end(), lightSort);
	
	for (int i = 0; i < this->render_calls.size(); ++i){
		RenderCall& rc = this->render_calls[i];
		//BoundingBox world_bounding = transformBoundingBox(rc.model, rc.mesh->box);
		if (camera->testBoxInFrustum(rc.boundingBox.center, rc.boundingBox.halfsize))
			renderMeshWithMaterial(rc.model, rc.mesh, rc.material, camera);
		
	}
}

//renders all the prefab
void Renderer::renderPrefab(const Matrix44& model, GTR::Prefab* prefab, Camera* camera)
{
	assert(prefab && "PREFAB IS NULL");
	//assign the model to the root node
	renderNode(model, &prefab->root, camera);
}

//renders a node of the prefab and its children
void Renderer::renderNode(const Matrix44& prefab_model, GTR::Node* node, Camera* camera)
{
	if (!node->visible)
		return;

	//compute global matrix
	Matrix44 node_model = node->getGlobalMatrix(true) * prefab_model;

	//does this node have a mesh? then we must render it
	if (node->mesh && node->material)
	{
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		//BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
		
		//if bounding box is inside the camera frustum then the object is probably visible
		//if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		//{
			//render node mesh
			//renderMeshWithMaterial( node_model, node->mesh, node->material, camera );
			//node->mesh->renderBounding(node_model, true);
		RenderCall rc;
		Vector3 nodepos = node_model.getTranslation();
		rc.mesh = node->mesh;
		rc.material = node->material;
		rc.model = node_model;
		rc.distance_to_camera = distance(nodepos,camera->eye);
		rc.boundingBox = transformBoundingBox(node_model, node->mesh->box);
		this->render_calls.push_back(rc);
			
		//}
	}

	//iterate recursively with children
	for (int i = 0; i < node->children.size(); ++i)
		renderNode(prefab_model, node->children[i], camera);
}


//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterial(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;
	Texture* texture = NULL;
	Texture* textureEmissive = NULL;
	Texture* textureMRT = NULL;
	Texture* textureNormal = NULL;
	Texture* textureOcclusion = NULL;
	GTR::Scene* scene = GTR::Scene::instance;



	texture = material->color_texture.texture;
	textureEmissive = material->emissive_texture.texture;
	textureMRT = material->metallic_roughness_texture.texture;
	textureNormal = material->normal_texture.texture;
	textureOcclusion = material->occlusion_texture.texture;
	if (texture == NULL)
		texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (textureEmissive == NULL)
		textureEmissive = Texture::getBlackTexture(); //a 1x1 white texture
	if (textureMRT == NULL)
		textureMRT = Texture::getWhiteTexture(); //a 1x1 white texture
	if (textureNormal == NULL)
		textureNormal = Texture::getWhiteTexture(); //a 1x1 white texture
	if (textureOcclusion == NULL)
		textureOcclusion = Texture::getWhiteTexture(); //a 1x1 white texture
	

	//select the blending
	if (material->alpha_mode == GTR::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);


	//select if render both sides of the triangles
	if(material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
    assert(glGetError() == GL_NO_ERROR);

	//chose a shader
	int num_lights = lights.size();
	
	
	shader = Shader::Get(num_lights==0?"noLights":this->multiLightType == 0 ? "singlePass" : "multiPass");
	

    assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model );
	shader->setFloat("u_emissive_factor", 1.0 );
	float t = getTime();
	shader->setUniform("u_time", t );

	shader->setUniform("u_color", material->color);
	if(texture)
		shader->setUniform("u_texture", texture, 0);
	
	if (textureEmissive)
		shader->setUniform("u_emissive_texture", textureEmissive, 1);
	if (textureMRT)
		shader->setUniform("u_metallic_roughness_texture", textureMRT, 2);
	if (textureNormal)
		shader->setUniform("u_normal_texture", textureNormal, 3);
	if (textureOcclusion)
		shader->setUniform("u_occlusion_texture", textureOcclusion, 4);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);
	
	shader->setUniform("u_ambient_light", scene->ambient_light);

	if (!num_lights) {
		mesh->render(GL_TRIANGLES);
		return;
	}

	if (this->multiLightType == (int)eMultiLightType::SINGLE_PASS) {
		const int maxLights = 5;

		std::vector<const char*> textureNames = { "u_shadow_texture0","u_shadow_texture1","u_shadow_texture2","u_shadow_texture3","u_shadow_texture4" };
		
		Vector3 light_position[maxLights] = {};
		Vector3 light_color[maxLights] = {};
		float light_max_distance[maxLights] = {};
		int light_type[maxLights] = {};
		Vector3 light_vector[maxLights] = {};
		float spotCosineCuttof[maxLights] = {};
		float coneAngle[maxLights] = {};
		float coneExp[maxLights] = {};
		int light_cast_shadows[maxLights] = {};
		//Texture* light_shadowmap[maxLights] = {};
		Matrix44 light_shadowmap_vp[maxLights] = {};
		float shadowBias[maxLights] = {};
		
		
		
		for (int i = 0; i < num_lights; i++) {
			light_position[i] = lights[i]->model.getTranslation();
			light_color[i] = lights[i]->color * lights[i]->intensity;
			light_max_distance[i] = lights[i]->max_distance;
			light_type[i] = (int)(lights[i]->light_type);
			light_vector[i] = lights[i]->lightDirection;
			coneAngle[i] = lights[i]->cone_angle;
			spotCosineCuttof[i] = cos((float)(DEG2RAD * lights[i]->cone_angle));
			coneExp[i] = lights[i]->cone_exp;
			light_cast_shadows[i] = (lights[i]->cast_shadows)?1:0;
			//light_shadowmap[i] = lights[i]->shadow_map;
			shadowBias[i] = lights[i]->shadow_bias;
			
			if ((lights[i]->shadow_map && lights[i]->cast_shadows)) {
				shader->setTexture(textureNames[i], lights[i]->shadow_map, 8 + i);
				light_shadowmap_vp[i] = lights[i]->shadow_cam->viewprojection_matrix;
			}
			

			
	
		};
		
		shader->setUniform3Array("u_light_position", (float*)&light_position, num_lights);
		shader->setUniform3Array("u_light_color", (float*)&light_color, num_lights);
		shader->setUniform1Array("u_light_max_distance", (float*)&light_max_distance, num_lights);
		shader->setUniform1Array("u_light_type", (int*)&light_type, num_lights);
		shader->setUniform3Array("u_light_vector", (float*)&light_vector, num_lights);
		shader->setUniform1Array("u_cone_angle", (float*)&coneAngle, num_lights);
		shader->setUniform1Array("u_cosine_cutoff", (float*)&spotCosineCuttof, num_lights);
		shader->setUniform1Array("u_cone_exp", (float*)&coneExp, num_lights);
		shader->setUniform1Array("u_cast_shadow", (int*)&light_cast_shadows, num_lights);
		
		
		
		
		
		shader->setMatrix44Array("u_shadow_map_vp", (Matrix44*)&light_shadowmap_vp, num_lights);
		
		shader->setUniform1Array("u_shadowBias", (float*)&shadowBias, num_lights);
		
		
		
		

		shader->setUniform1("u_num_lights", num_lights);


	mesh->render(GL_TRIANGLES);
	shader->disable();
	}
	else {
		glDepthFunc(GL_LEQUAL);
		if (material->alpha_mode == GTR::eAlphaMode::BLEND)
			glEnable(GL_BLEND);
		else
			glDisable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, material->alpha_mode==GTR::eAlphaMode::BLEND? GL_ONE_MINUS_SRC_ALPHA: GL_ONE);
		
		
	
		for (int i = 0; i < num_lights; i++) {
			LightEntity* light = lights[i];
			if (i > 0) {
				shader->setUniform("u_ambient_light", Vector3());
				shader->setFloat("u_emissive_factor", 0.0f);
			}
			
			shader->setUniform("u_light_color", light->color * light->intensity);
			shader->setUniform("u_light_position", light->model.getTranslation());
			shader->setUniform("u_light_max_distance", light->max_distance);
			shader->setUniform("u_light_type", (int)light->light_type);
			shader->setUniform("u_light_vector", light->lightDirection);
			shader->setUniform("u_spotCosineCuttof", 0.0f);
			shader->setUniform("u_cone_angle", 0.0f);
			
			shader->setUniform("u_light_cast_shadows", false);
			
			
			
			if (light->light_type== eLightType::SPOT){
				shader->setUniform("u_cone_angle", light->cone_angle);
				shader->setUniform("u_cone_exp", light->cone_exp);	
				
				//get radians of light-> cone_angle
				
				//float radians = light->cone_angle * (float)M_PI / 180.0f;
				
				shader->setUniform("u_spotCosineCuttof",cos((float) (DEG2RAD* light->cone_angle)));	
				
				
				//get radians of cone angle
				
			}
			if (light->shadow_map && light->cast_shadows) {
				shader->setUniform("u_light_cast_shadows", true);
				shader->setUniform("u_light_shadowmap", light->shadow_map,8);
				shader->setUniform("u_light_shadowmap_vp",light->shadow_cam->viewprojection_matrix);
				shader->setUniform("u_shadow_bias", light->shadow_bias);

			}
			
			
			mesh->render(GL_TRIANGLES);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA,  GL_ONE);
		}



		glDisable(GL_BLEND);
		glDepthFunc(GL_LESS);
	}

	//do the draw call that renders the mesh into the screen

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
}


void Renderer::renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera) {
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	//define locals to simplify coding
	Shader* shader = NULL;
	GTR::Scene* scene = GTR::Scene::instance;
	/*if (material->alpha_mode == GTR::eAlphaMode::BLEND)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);

*/
	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	
	shader = Shader::Get("flat");
	shader->enable();
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_model", model);
	
	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);
	mesh->render(GL_TRIANGLES);
	shader->disable();
}

void GTR::Renderer::generateShadowMaps(LightEntity* light)
{
	if (!light->cast_shadows) {
		if (light->shadow_fbo) {
			delete light->shadow_fbo;
			light->shadow_fbo = NULL;
			light->shadow_map = NULL;
			
		}
	
	};
	
	if (!light->shadow_fbo)
	{
		light->shadow_fbo = new FBO();
		light->shadow_fbo->setDepthOnly(2048,2048);
		light->shadow_map = light->shadow_fbo->depth_texture;
	}
	Camera* view_cam = Camera::current;
	
	if (!light->shadow_cam)
		light->shadow_cam = new Camera();

	//enable it to render inside the texture
	light->shadow_fbo->bind();

	
	
	//you can disable writing to the color buffer to speed up the rendering as we do not need it
	
	if (light->light_type == eLightType::DIRECTIONAL) {
		light->shadow_cam->setOrthographic(-light->area_size / 2, light->area_size / 2, light->area_size / 2, -light->area_size / 2, .1, light->max_distance);
		
		light->shadow_cam->lookAt(light->model.getTranslation(), light->model.getTranslation() - (light->lightDirection*20), Vector3(0, 1, 0));

		//light->shadow_cam->lookAt(light->model.getTranslation(), light->model.getTranslation() + light->lightDirection, Vector3(0, -1, 0));
	}
	else if(light->light_type== eLightType::SPOT) {
		light->shadow_cam->setPerspective(light->cone_angle, 1.0, 0.1, light->max_distance);
		light->shadow_cam->lookAt(light->model.getTranslation(), light->model * Vector3(0, 0, -1), light->model.rotateVector(Vector3(0, 1, 0)));
	}
		light->shadow_cam->enable();
	glColorMask(false, false, false, false);

	//clear the depth buffer only (don't care of color)
	glClear(GL_DEPTH_BUFFER_BIT);

	//whatever we render here will be stored inside a texture, we don't need to do anything fanzy
	//...
	/*Shader* shader = NULL;
	shader = Shader::Get("flat");
	shader->enable();*/
	
	for (int i = 0; i < this->render_calls.size(); ++i) {
		RenderCall& rc = this->render_calls[i];
		if (rc.material->alpha_mode == eAlphaMode::BLEND)
			continue;
		if (light->shadow_cam->testBoxInFrustum(rc.boundingBox.center, rc.boundingBox.halfsize))
			renderFlatMesh(rc.model, rc.mesh, rc.material, light->shadow_cam);
		//shader->setUniform("u_viewprojection", light->shadow_cam->viewprojection_matrix);
		//rc.mesh->render(GL_TRIANGLES);

	}
	
	//disable it to render back to the screen
	light->shadow_fbo->unbind();

	//allow to render back to the color buffer
	glColorMask(true, true, true, true);

	view_cam->enable();

}



Texture* GTR::CubemapFromHDRE(const char* filename)
{
	HDRE* hdre = HDRE::Get(filename);
	if (!hdre)
		return NULL;

	Texture* texture = new Texture();
	if (hdre->getFacesf(0))
	{
		texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesf(0),
			hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_FLOAT);
		for (int i = 1; i < hdre->levels; ++i)
			texture->uploadCubemap(texture->format, texture->type, false,
				(Uint8**)hdre->getFacesf(i), GL_RGBA32F, i);
	}
	else
		if (hdre->getFacesh(0))
		{
			texture->createCubemap(hdre->width, hdre->height, (Uint8**)hdre->getFacesh(0),
				hdre->header.numChannels == 3 ? GL_RGB : GL_RGBA, GL_HALF_FLOAT);
			for (int i = 1; i < hdre->levels; ++i)
				texture->uploadCubemap(texture->format, texture->type, false,
					(Uint8**)hdre->getFacesh(i), GL_RGBA16F, i);
		}
	return texture;
}
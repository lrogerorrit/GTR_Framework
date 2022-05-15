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
#include "application.h"
#include <algorithm>
#include <string>
#include "shadowAtlas.h"


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
		return (int)a->light_type > (int)b->light_type;
	else
		return (int)a->cast_shadows > (int)b->cast_shadows ;
}
	



GTR::Renderer::Renderer()
{
	this->shadowMapAtlas = new shadowAtlas();
}

void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
	
	
	//render entities

	this->render_calls.clear();
	this->lights.clear();
	this->shadowMapAtlas->clearArray();

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

	

		std::sort(this->lights.begin(), this->lights.end(), lightSort);
	if (this->orderNodes)
		std::sort(this->render_calls.begin(), this->render_calls.end(), transparencySort);
	

	//generate shadowmaps
	for (int i = 0; i < lights.size(); ++i) {
		LightEntity* light = lights[i];
		if (light->cast_shadows)
			this->shadowMapAtlas->addLight(light);
			///generateShadowMaps(light);
	}
	this->shadowMapAtlas->calculateShadows(this->render_calls);
	
//	this->shadowMapAtlas->atlasFBO->depth_texture->toViewport();

	std::sort(this->lights.begin(), this->lights.end(), lightSort);
	
	if (pipelineType==ePipeLineType::FORWARD)
		RenderForward(camera,scene );
	else
		RenderDeferred(camera,scene);
	
	
	
	
	
	
	
}

void GTR::Renderer::RenderForward(Camera* camera, GTR::Scene* scene)
{
	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	checkGLErrors();

	for (int i = 0; i < this->render_calls.size(); ++i) {
		RenderCall& rc = this->render_calls[i];
		//BoundingBox world_bounding = transformBoundingBox(rc.model, rc.mesh->box);
		if (camera->testBoxInFrustum(rc.boundingBox.center, rc.boundingBox.halfsize))
			renderMeshWithMaterialAndLighting(rc.model, rc.mesh, rc.material, camera);

	}
}

void GTR::Renderer::RenderDeferred(Camera* camera, GTR::Scene* scene)
{
	//Render GBuffer
	//Create gbuffer if it doesn't exist

	int width= Application::instance->window_width;
	int height= Application::instance->window_height;
	if (!this->gbuffers_fbo) {
		gbuffers_fbo= new FBO();
		illumination_fbo= new FBO();
		
		gbuffers_fbo->create(width,height,
			3, 			//three textures
			GL_RGBA, 		//four channels
			GL_UNSIGNED_BYTE, //1 byte
			true);		//add depth_texture
		
		illumination_fbo->create(width, height,
			1, 					//one textures
			GL_RGB,				//three channels
			GL_FLOAT, //1 byte
			true);				//add depth_texture

	}
		
	gbuffers_fbo->bind();

	glClearColor(0,0,0, 1.0);
	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	
	//Render every object with a gbuffer shader
	for (int i = 0; i < this->render_calls.size(); ++i) {
		RenderCall& rc = this->render_calls[i];
		//BoundingBox world_bounding = transformBoundingBox(rc.model, rc.mesh->box);
		if (camera->testBoxInFrustum(rc.boundingBox.center, rc.boundingBox.halfsize))
			renderMeshWithMaterialToGBuffers(rc.model, rc.mesh, rc.material, camera);

	}
	gbuffers_fbo->unbind();
	//render to screen
	illumination_fbo->bind();
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
	
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	Mesh* quad = Mesh::getQuad();
	Mesh* sphere= Mesh::Get("data/meshes/sphere.obj");
	
	Shader* shader = Shader::Get((this->isOptimizedDeferred)?"deferred_opti":"deferred");
	shader->enable();
	shader->setUniform("u_ambient_light", scene->ambient_light);

	shader->setUniform("u_gb0_texture", gbuffers_fbo->color_textures[0], 0);
	shader->setUniform("u_gb1_texture", gbuffers_fbo->color_textures[1], 1);
	shader->setUniform("u_gb2_texture", gbuffers_fbo->color_textures[2], 2);
	shader->setUniform("u_depth_texture", gbuffers_fbo->depth_texture, 3);

	//pass the inverse projection of the camera to reconstruct world pos.
	Matrix44 inv_vp = camera->viewprojection_matrix;
	inv_vp.inverse();
	shader->setUniform("u_inverse_viewprojection", inv_vp);
	//pass the inverse window resolution, this may be useful
	shader->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));
	this->shadowMapAtlas->uploadDataToShader(shader,this->lights);

	if (this->isOptimizedDeferred) {
		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		glEnable(GL_CULL_FACE);
	}

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	if (!lights.size()) {
		shader->setUniform("u_ambient_light", Vector3());
		quad->render(GL_TRIANGLES);
		return;
	}else
		for (int i = 0; i < lights.size(); ++i)
		{
			
			LightEntity* light = lights[i];
			if (i == 0)
				glDisable(GL_BLEND);
			else
			{
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);
				glEnable(GL_BLEND);
			}
			
			Matrix44 m;
			
			m.setTranslation(light->model.getTranslation());
			m.scale(light->max_distance, light->max_distance, light->max_distance);

			shader->setUniform("u_model", m);

			
			uploadSingleLightToShader(shader, light);
			shader->setUniform("light_index", i);
			
			if (this->isOptimizedDeferred) {
				glFrontFace(GL_CW);
				sphere->render(GL_TRIANGLES);
				glFrontFace(GL_CCW);
			}else
				quad->render(GL_TRIANGLES);
			
			shader->setUniform("u_ambient_light", Vector3());
			
		}
		
	shader->disable();
	
	illumination_fbo->unbind();
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	illumination_fbo->color_textures[0]->toViewport();
	//Apply multipass reading to gbuffers


	if (showGBuffers) {
		glViewport(0, height * .5, width * .5, height * .5);
		gbuffers_fbo->color_textures[0]->toViewport();
		glViewport(width*.5, height * .5, width * .5, height * .5);
		gbuffers_fbo->color_textures[1]->toViewport();
		glViewport(0, 0, width * .5, height * .5);
		gbuffers_fbo->color_textures[2]->toViewport();
		glViewport(width*.5,0, width * .5, height * .5);
		Shader* shaderb = Shader::getDefaultShader("depth");
		shaderb->enable();
		shaderb->setUniform("u_camera_nearfar", Vector2(camera->near_plane, camera->far_plane));
		gbuffers_fbo->depth_texture->toViewport(shaderb);
		shaderb->disable();
		glViewport(0, 0,width,height);
		
	
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
		/*
		//compute the bounding box of the object in world space (by using the mesh bounding box transformed to world space)
		//BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
		
		//if bounding box is inside the camera frustum then the object is probably visible
		//if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		//{
			//render node mesh
			//renderMeshWithMaterialAndLighting( node_model, node->mesh, node->material, camera );
			//node->mesh->renderBounding(node_model, true);
		*/
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


void GTR::Renderer::renderMeshWithMaterialToGBuffers(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
{
	//in case there is nothing to do
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);

	if (material->alpha_mode == eAlphaMode::BLEND) return;

	//define locals to simplify coding
	Shader* shader = NULL;
	Texture* texture = NULL;
	Texture* textureEmissive = NULL;
	Texture* textureMRT = NULL;
	Texture* textureNormal = NULL;

	//GTR::Scene* scene = Application::instance->getActiveScene();



	texture = material->color_texture.texture;
	textureEmissive = material->emissive_texture.texture;
	textureMRT = material->metallic_roughness_texture.texture;
	textureNormal = material->normal_texture.texture;

	if (texture == NULL) texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (textureEmissive == NULL) textureEmissive = Texture::getBlackTexture(); //a 1x1 white texture
	if (textureMRT == NULL) textureMRT = Texture::getWhiteTexture(); //a 1x1 white texture
	//if (textureNormal == NULL)  //textureNormal = Texture::getWhiteTexture(); //a 1x1 white texture


	//select if render both sides of the triangles
	if (material->two_sided)
		glDisable(GL_CULL_FACE);
	else
		glEnable(GL_CULL_FACE);
	assert(glGetError() == GL_NO_ERROR);

	//chose a shader
	
	shader = Shader::Get("gbuffers");


	assert(glGetError() == GL_NO_ERROR);

	//no shader? then nothing to render
	if (!shader)
		return;
	shader->enable();

	//upload uniforms
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);

	shader->setFloat("u_emissive_factor", this->useEmissive ? 1.0 : 0.0);
	shader->setUniform("u_use_normalmap", this->useNormalMap);
	shader->setFloat("u_useOcclusion", this->useOcclusion);
	float t = getTime();
	shader->setUniform("u_time", t);

	shader->setUniform("u_color", material->color);
	if (texture)
		shader->setUniform("u_texture", texture, 0);

	if (textureEmissive)
		shader->setUniform("u_emissive_texture", textureEmissive, 1);
	if (textureMRT)
		shader->setUniform("u_metallic_roughness_texture", textureMRT, 2);
	if (textureNormal)
		shader->setUniform("u_normal_texture", textureNormal, 3);
	else
		shader->setUniform("u_use_normalmap", false);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);
	
	mesh->render(GL_TRIANGLES);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	
	glDisable(GL_BLEND);
	glDepthFunc(GL_LESS);
	

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
	

}

void GTR::Renderer::uploadSingleLightToShader(Shader* shader, GTR::LightEntity* light)
{
	shader->setUniform("u_light_color", light->color * light->intensity);
	shader->setUniform("u_light_position", light->model.getTranslation());
	shader->setUniform("u_light_max_distance", light->max_distance);
	shader->setUniform("u_light_type", (int)light->light_type);
	shader->setUniform("u_light_vector", light->lightDirection);
	shader->setUniform("u_spotCosineCuttof", 0.0f);
	shader->setUniform("u_cone_angle", 0.0f);
	shader->setUniform("u_light_cast_shadows", false);
	if (light->light_type == eLightType::SPOT) {
		shader->setUniform("u_cone_angle", light->cone_angle);
		shader->setUniform("u_cone_exp", light->cone_exp);
		shader->setUniform("u_spotCosineCuttof", cos((float)(DEG2RAD * light->cone_angle)));
	}
	if (light->has_shadow_map && light->cast_shadows) {
		shader->setUniform("u_light_cast_shadows", true);
		//shader->setUniform("u_light_shadowmap", light->shadow_map,8);
		shader->setUniform("u_light_shadowmap_vp", light->shadow_cam->viewprojection_matrix);
		shader->setUniform("u_shadow_bias", light->shadow_bias);
	}
}

//renders a mesh given its transform and material
void Renderer::renderMeshWithMaterialAndLighting(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera)
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
	
	GTR::Scene* scene = Application::instance->getActiveScene();



	texture = material->color_texture.texture;
	textureEmissive = material->emissive_texture.texture;
	textureMRT = material->metallic_roughness_texture.texture;
	textureNormal = material->normal_texture.texture;
	
	if (texture == NULL)
		texture = Texture::getWhiteTexture(); //a 1x1 white texture
	if (textureEmissive == NULL)
		textureEmissive = Texture::getBlackTexture(); //a 1x1 white texture
	if (textureMRT == NULL)
		textureMRT = Texture::getWhiteTexture(); //a 1x1 white texture
	if (textureNormal == NULL)
		textureNormal = Texture::getWhiteTexture(); //a 1x1 white texture
	
	

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
	
	shader->setFloat("u_emissive_factor", this->useEmissive?1.0:0.0 );
	shader->setUniform("u_use_normalmap", this->useNormalMap);
	shader->setFloat("u_useOcclusion", this->useOcclusion);
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
			
			if ((lights[i]->has_shadow_map && lights[i]->cast_shadows)) {
				//shader->setTexture(textureNames[i], lights[i]->shadow_map, 8 + i);
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
		this->shadowMapAtlas->uploadDataToShader(shader,this->lights);
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
		
		
		this->shadowMapAtlas->uploadDataToShader(shader, this->lights);
		for (int i = 0; i < num_lights; i++) {
			LightEntity* light = lights[i];
			if (i > 0) {
				shader->setUniform("u_ambient_light", Vector3());
				shader->setFloat("u_emissive_factor", 0.0f);
			}
			shader->setUniform("light_index", i);
			
			uploadSingleLightToShader(shader, light);
			mesh->render(GL_TRIANGLES);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA,  GL_ONE);
		}
		glDisable(GL_BLEND);
		glDepthFunc(GL_LESS);
	}

	//disable shader
	shader->disable();

	//set the render state as it was before to avoid problems with future renders
	glDisable(GL_BLEND);
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
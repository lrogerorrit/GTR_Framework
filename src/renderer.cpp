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
#include <algorithm>



//function to calculate distance between two vector3;
float distance(Vector3 a, Vector3 b)
{
	return sqrt(pow(a.x - b.x, 2) + pow(a.y - b.y, 2) + pow(a.z - b.z, 2));
}

//Note to self, this is for < operator
bool transparencySort(const GTR::RenderCall a, const GTR::RenderCall b) {
	if (b.material->alpha_mode > 0 && a.material->alpha_mode > 0) //if both transparent order from far to near
		return a.distance_to_camera > b.distance_to_camera;
	else if (b.material->alpha_mode == 0 && a.material->alpha_mode == 0) //if both opaque order from near to far
		return a.distance_to_camera < b.distance_to_camera;
	else
		return a.material->alpha_mode == 0; //else put opaque before transparent
}

using namespace GTR;

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
	for (int i = 0; i < this->render_calls.size(); ++i){
		RenderCall& rc = this->render_calls[i];
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
		BoundingBox world_bounding = transformBoundingBox(node_model,node->mesh->box);
		
		//if bounding box is inside the camera frustum then the object is probably visible
		if (camera->testBoxInFrustum(world_bounding.center, world_bounding.halfsize) )
		{
			//render node mesh
			//renderMeshWithMaterial( node_model, node->mesh, node->material, camera );
			//node->mesh->renderBounding(node_model, true);
			RenderCall rc;
			Vector3 nodepos = node_model.getTranslation();
			rc.mesh = node->mesh;
			rc.material = node->material;
			rc.model = node_model;
			rc.distance_to_camera = distance(nodepos,camera->eye);
			this->render_calls.push_back(rc);
			
		}
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
	GTR::Scene* scene = GTR::Scene::instance;



	texture = material->color_texture.texture;
	//texture = material->emissive_texture;
	//texture = material->metallic_roughness_texture;
	//texture = material->normal_texture;
	//texture = material->occlusion_texture;
	if (texture == NULL)
		texture = Texture::getWhiteTexture(); //a 1x1 white texture

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
	float t = getTime();
	shader->setUniform("u_time", t );

	shader->setUniform("u_color", material->color);
	if(texture)
		shader->setUniform("u_texture", texture, 0);

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);
	
	shader->setUniform("u_ambient_light", scene->ambient_light);

	if (!num_lights) {
		mesh->render(GL_TRIANGLES);
		return;
	}

	if (this->multiLightType ==  (int) eMultiLightType::SINGLE_PASS) {
		Vector3 light_position[5] = {};
		Vector3 light_color[5] = {};
		float light_max_distance[5] = {};

		for (int i = 0; i < num_lights; ++i) {
			light_position[i] = lights[i]->model.getTranslation();
			light_color[i] = lights[i]->color*lights[i]->intensity;
			light_max_distance[i] = lights[i]->max_distance;
			
		}
		shader->setUniform3Array("u_light_pos", (float*)&light_position, num_lights);
		shader->setUniform3Array("u_light_color", (float*)&light_color, num_lights);
		shader->setUniform1Array("u_max_distance", (float*)&light_max_distance, num_lights);
		shader->setUniform1("u_num_lights", num_lights);


	mesh->render(GL_TRIANGLES);
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
			if (i>0)
				shader->setUniform("u_ambient_light", Vector3());
			
			shader->setUniform("u_light_color", light->color * light->intensity);
			shader->setUniform("u_light_position", light->model.getTranslation());
			shader->setUniform("u_light_max_distance", light->max_distance);
			shader->setUniform("u_light_type", (int)light->light_type);
			shader->setUniform("u_target_pos", light->target);
			
			
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
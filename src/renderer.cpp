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
	int width = Application::instance->window_width;
	int height = Application::instance->window_height;
	skybox = CubemapFromHDRE("data/night.hdre");
	reflection_fbo = new FBO();
	reflection_fbo->create(
		width,
		height
	);
	

	this->shadowMapAtlas = new shadowAtlas();
	//Generate random points
	generateSpherePoints(64, 1, false);
	
}

void Renderer::renderSkybox(Camera* camera)
{
	Mesh* mesh = Mesh::Get("data/meshes/sphere.obj");
	Shader* shader = Shader::Get("skybox");
	shader->enable();

	Matrix44 model;
	
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	
	model.setTranslation(camera->eye);
	model.scale(5, 5, 5);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform("u_texture",skybox, 1);

	mesh->render(GL_TRIANGLES);
	shader->disable();
	
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	

}

void Renderer::renderScene(GTR::Scene* scene, Camera* camera)
{
	
	
	//render entities

	this->render_calls.clear();
	this->lights.clear();
	this->decals.clear();
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
		else if (ent->entity_type == eEntityType::DECAL) {
			decals.push_back((DecalEntity*) ent);
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
			//generateShadowMaps(light);
	}
	this->shadowMapAtlas->calculateShadows(this->render_calls);
	
//	this->shadowMapAtlas->atlasFBO->depth_texture->toViewport();

	std::sort(this->lights.begin(), this->lights.end(), lightSort);
	
	if (shouldCalculateProbes) {
		this->shouldCalculateProbes = false;
		this->CalculateIrradianceProbes(scene);
	}
	
	if (pipelineType==ePipeLineType::FORWARD)
		RenderForward(camera,scene );
	else
		RenderDeferred(camera,scene);
	
	if (displayIRRProbes) {
		
		for (int i = 0; i < irrProbes.size(); ++i) {
			sProbe& data= irrProbes[i];
			float* coeff = (float*) & data.sh.coeffs[0];
			renderProbe(data.pos, 5.0, (float*)&data.sh.coeffs);
		}
	}
	if (displayReflectionProbes) {
		renderReflectionProbes(scene, camera);
	}
	
	
	
	
	
}



void GTR::Renderer::RenderForward(Camera* camera, GTR::Scene* scene)
{
	//set the clear color (the background color)
	glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	checkGLErrors();

	renderSkybox(camera);

	for (int i = 0; i < this->render_calls.size(); ++i) {
		RenderCall& rc = this->render_calls[i];
		//BoundingBox world_bounding = transformBoundingBox(rc.model, rc.mesh->box);
		if (camera->testBoxInFrustum(rc.boundingBox.center, rc.boundingBox.halfsize))
			renderMeshWithMaterialAndLighting(rc.model, rc.mesh, rc.material, camera);

	}
}



void Renderer::renderProbe(Vector3 pos, float size, float* coeffs)
{
	Camera* camera = Camera::current;
	Shader* shader = Shader::Get("probe");
	Mesh* mesh = Mesh::Get("data/meshes/sphere.obj");

	glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);

	Matrix44 model;
	model.setTranslation(pos.x, pos.y, pos.z);
	model.scale(size, size, size);

	shader->enable();
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_pos", camera->eye);
	shader->setUniform("u_model", model);
	shader->setUniform3Array("u_coeffs", coeffs, 9);

	mesh->render(GL_TRIANGLES);
}

void Renderer::CalculateProbe(sProbe& probe,Camera* cam,Scene* scene) {
	FloatImage images[6]; //here we will store the six views

	//set the fov to 90 and the aspect to 1
	cam->setPerspective(90, 1, 0.1, 1000);

	if (!this->irradiance_fbo) {
		this->irradiance_fbo = new FBO();
		this->irradiance_fbo->create(
			64,
			64,
			1,
			GL_RGB, //CAL LA A?
			GL_FLOAT,
			false);
		
	}
	
	for (int i = 0; i < 6; ++i) //for every cubemap face
	{
		//compute camera orientation using defined vectors
		Vector3 eye = probe.pos;
		Vector3 front = cubemapFaceNormals[i][2];
		Vector3 center = probe.pos + front;
		Vector3 up = cubemapFaceNormals[i][1];
		cam->lookAt(eye, center, up);
		cam->enable();

		//render the scene from this point of view
		irradiance_fbo->bind();
		this->RenderForward(cam, scene);
		irradiance_fbo->unbind();

		//read the pixels back and store in a FloatImage
		images[i].fromTexture(irradiance_fbo->color_textures[0]);
	}
	probe.sh = computeSH(images);	
	
}

void Renderer::CreateIrradianceGrid() {
	//when computing the probes position…

	//define the corners of the axis aligned grid
	//this can be done using the boundings of our scene
	Vector3 start_pos(-300, 5, -400);
	Vector3 end_pos(300, 150, 400);
	start_irr = start_pos;
	end_irr = end_pos;

	//define how many probes you want per dimension
	irr_probe_dim=Vector3(10, 4, 10);

	//compute the vector from one corner to the other
	Vector3 delta = (end_pos - start_pos);

	//and scale it down according to the subdivisions
	//we substract one to be sure the last probe is at end pos
	delta.x /= (irr_probe_dim.x - 1);
	delta.y /= (irr_probe_dim.y - 1);
	delta.z /= (irr_probe_dim.z - 1);

	//now delta give us the distance between probes in every axis
	
	//lets compute the centers
	//pay attention at the order at which we add them
	std::cout << "Creating irr grid\r";
	this->irrProbes.reserve(irr_probe_dim.x * irr_probe_dim.y * irr_probe_dim.z);
	for (int z = 0; z < irr_probe_dim.z; ++z)
		for (int y = 0; y < irr_probe_dim.y; ++y)
			for (int x = 0; x < irr_probe_dim.x; ++x)
			{
				sProbe p;
				p.local.set(x, y, z);

				//index in the linear array
				p.index = x + y * irr_probe_dim.x + z * irr_probe_dim.x * irr_probe_dim.y;

				//and its position
				p.pos = start_pos + delta * Vector3(x, y, z);
				this->irrProbes.push_back(p);
			}
	std::cout << "Creating irr grid: DONE\n";
}

void Renderer::CalculateAllProbes(Scene* scene) {
	Camera* probeCam = new Camera();
	
	if (!this->irrProbes.size()) {
		CreateIrradianceGrid();
	}

	for (int i = 0; i < this->irrProbes.size(); ++i) {
		sProbe& p = this->irrProbes[i];
		std::cout << "Calculating probe: " << i+1 << "/"<<this->irrProbes.size() << "\r";
		CalculateProbe(p, probeCam, scene);
		
	}
	std::cout << "Calculating probe: DONE             \n";
	
	
}

void Renderer::StoreProbesToTexture() {
	
	//create the texture to store the probes (do this ONCE!!!)
	irr_probe_texture = new Texture(
		9, //9 coefficients per probe
		irrProbes.size(), //as many rows as probes
		GL_RGB, //3 channels per coefficient
		GL_FLOAT); //they require a high range

		//we must create the color information for the texture. because every SH are 27 floats in the RGB,RGB,... order, we can create an array of SphericalHarmonics and use it as pixels of the texture
	SphericalHarmonics* sh_data = NULL;
	sh_data = new SphericalHarmonics[irr_probe_dim.x * irr_probe_dim.y * irr_probe_dim.z];

	//here we fill the data of the array with our probes in x,y,z order
	for (int i = 0; i < irrProbes.size(); ++i)
		sh_data[i] = irrProbes[i].sh;

	//now upload the data to the GPU as a texture
	irr_probe_texture->upload(GL_RGB, GL_FLOAT, false, (uint8*)sh_data);

	//disable any texture filtering when reading
	irr_probe_texture->bind();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	//always free memory after allocating it!!!
	delete[] sh_data;

}

void GTR::Renderer::renderSSAO(Camera* cam, GTR::Scene* scene,Matrix44& invVP,Mesh* quad) {
	// start rendering inside the ssao texture
	ssao_fbo->bind();
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	if (!randomPoints.size()) {
		
		randomPoints.clear();
		randomPoints = generateSpherePoints(64, 1, true);
		
	}
	
	//get the shader for SSAO (remember to create it using the atlas)
	Shader* shader = Shader::Get("ssao");
	shader->enable();

	//send info to reconstruct the world position
	shader->setUniform("u_inverse_viewprojection", invVP);
	shader->setTexture("u_depth_texture", gbuffers_fbo->depth_texture, 3);
	shader->setTexture("u_normal_texture", gbuffers_fbo->color_textures[1], 4);
	//we need the pixel size so we can center the samples 
	shader->setUniform("u_iRes", Vector2(1.0 / (float)gbuffers_fbo->depth_texture->width,
		1.0 / (float)gbuffers_fbo->depth_texture->height));
	//we will need the viewprojection to obtain the uv in the depthtexture of any random position of our world
	shader->setUniform("u_viewprojection", cam->viewprojection_matrix);

	//send random points so we can fetch around
	shader->setUniform3Array("u_points", (float*)&randomPoints[0],
		randomPoints.size());

	//render fullscreen quad
	quad->render(GL_TRIANGLES);
	shader->disable();
	//stop rendering to the texture
	ssao_fbo->unbind();
	ssao_fbo->bind();
	
	if (this->useSSAOBlur) {
		
		glClear( GL_DEPTH_BUFFER_BIT);
		Shader* shaderb = Shader::Get("ssaoBlur");
		shaderb->enable();
		shaderb->setUniform("u_inverse_viewprojection", invVP);
		shaderb->setUniform("u_ssao_texture", ssao_fbo->color_textures[0], 3);
		shaderb->setUniform("u_iRes", Vector2(1.0 / (float)ssao_fbo->color_textures[0]->width,
			1.0 / (float)ssao_fbo->color_textures[0]->height));
		quad->render(GL_TRIANGLES);
		shaderb->disable();	
	}
	ssao_fbo->unbind();

	

}

void Renderer::CalculateIrradianceProbes(Scene* scene) {
	this->CalculateAllProbes(scene);
	this->StoreProbesToTexture();
}

void GTR::Renderer::RenderDeferred(Camera* camera, GTR::Scene* scene)
{
	//Render GBuffer
	//Create gbuffer if it doesn't exist

	int width= Application::instance->window_width;
	int height= Application::instance->window_height;
	if (!this->gbuffers_fbo) {
		cube = new Mesh();
		cube->createCube();
		gbuffers_fbo= new FBO();
		illumination_fbo= new FBO();
		ssao_fbo = new FBO();
		tonemapper_fbo = new FBO();
		deferred_alpha_fbo = new FBO();
		volumetric_fbo = new FBO();
		decals_fbo = new FBO();
		DoF_fbo = new FBO();
		
		gbuffers_fbo->create(width,height,
			4, 			//three textures
			GL_RGBA, 		//four channels
			GL_FLOAT, //1 byte
			true);		//add depth_texture
		
		illumination_fbo->create(width, height,
			1, 					//one textures
			GL_RGB,				//three channels
			GL_FLOAT, //1 byte
			true);				//add depth_texture
		
		ssao_fbo->create(width,height,
			2,
			GL_LUMINANCE,
			GL_FLOAT,
			false);
		tonemapper_fbo->create(width, height,
			1,
			GL_RGB,
			GL_FLOAT,
			false);
		deferred_alpha_fbo->create(width, height,
			1,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			true);
		volumetric_fbo->create(width, height, 1, GL_RGBA);

		decals_fbo->create(width, height,
			3,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			true);
		DoF_fbo->create(width, height,
			2,
			GL_RGBA,
			GL_UNSIGNED_BYTE,
			true);
		
	}
	//bind the texture we want to change
	gbuffers_fbo->depth_texture->bind();

	//disable using mipmaps
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	//enable bilinear filtering
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	
	gbuffers_fbo->depth_texture->unbind();
	
	
	
	
	
	renderSkybox(Camera::current);
	gbuffers_fbo->bind();
	glClearColor(0, 0, 0, 1.0);
	// Clear the color and the depth buffer
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	

	std::vector<RenderCall*> alphaNodes;
	alphaNodes.clear();
	//Render every object with a gbuffer shader
	for (int i = 0; i < this->render_calls.size(); ++i) {
		RenderCall& rc = this->render_calls[i];
		//BoundingBox world_bounding = transformBoundingBox(rc.model, rc.mesh->box);
		if (camera->testBoxInFrustum(rc.boundingBox.center, rc.boundingBox.halfsize))
			if (rc.material->alpha_mode == eAlphaMode::BLEND)
				alphaNodes.push_back(&rc);
			else
				renderMeshWithMaterialToGBuffers(rc.model, rc.mesh, rc.material, camera);
	}
	gbuffers_fbo->unbind();

	Matrix44 inv_vp = camera->viewprojection_matrix;
	Matrix44 inv_view = camera->view_matrix;
	inv_vp.inverse();
	inv_view.inverse();
	
	//render decals
	gbuffers_fbo->color_textures[1]->copyTo(decals_fbo->color_textures[0]);
	gbuffers_fbo->color_textures[2]->copyTo(decals_fbo->color_textures[1]);
	gbuffers_fbo->color_textures[3]->copyTo(decals_fbo->color_textures[2]);
	decals_fbo->bind();
	gbuffers_fbo->depth_texture->copyTo(NULL);
	decals_fbo->unbind();
	

	gbuffers_fbo->bind();
	Shader* shader = Shader::Get("decal");
	shader->enable();
	shader->setUniform("u_gb0_texture", decals_fbo->color_textures[0], 0);
	shader->setUniform("u_gb1_texture", decals_fbo->color_textures[1], 1);
	shader->setUniform("u_gb2_texture", decals_fbo->color_textures[2], 2);
	shader->setUniform("u_depth_texture", decals_fbo->depth_texture, 3);
	
	shader->setUniform("u_inverse_viewprojection", inv_vp);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);

	shader->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	for (int i = 0; i < decals.size(); ++i) {
		DecalEntity* decal = decals[i];
		Matrix44 imodel = decal->model;
		imodel.inverse();
		if (decal->texture=="") continue;
		Texture* decal_texture = Texture::Get(decal->texture.c_str());
		if (!decal_texture) continue;
		shader->setUniform("u_decal_texture", decal_texture, 5);
		shader->setUniform("u_model", decal->model);
		shader->setUniform("u_imodel", imodel);
		cube->render(GL_TRIANGLES);

	}
	glDisable(GL_BLEND);


	gbuffers_fbo->unbind();

	//
	
	
	Mesh* quad = Mesh::getQuad();
	Mesh* sphere= Mesh::Get("data/meshes/sphere.obj");

	renderSSAO(camera, scene,inv_vp,quad);
	
	//render to screen
	illumination_fbo->bind();
	//glClearColor(scene->background_color.x, scene->background_color.y, scene->background_color.z, 1.0);
	
	
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	
	Texture* reflection = skybox;
	//Shader* shader = Shader::Get((this->isOptimizedDeferred)?"deferred_opti":"deferred");
	shader = Shader::Get("deferred");
	shader->enable();
	shader->setUniform("u_ambient_light", scene->ambient_light);
	shader->setUniform("u_camera_position", camera->eye);
	shader->setUniform("u_gb0_texture", gbuffers_fbo->color_textures[0], 0);
	shader->setUniform("u_gb1_texture", gbuffers_fbo->color_textures[1], 1);
	shader->setUniform("u_gb2_texture", gbuffers_fbo->color_textures[2], 2);
	shader->setUniform("u_gb3_texture", gbuffers_fbo->color_textures[3], 3);
	shader->setUniform("u_depth_texture", gbuffers_fbo->depth_texture, 4);
	shader->setFloat("u_emissive_factor", this->useEmissive ? 1.0 : 0.0);
	shader->setUniform("u_use_normalmap", this->useNormalMap);
	shader->setFloat("u_useOcclusion", this->useOcclusion);
	shader->setUniform("u_use_SSAO", this->useSSAO);
	shader->setUniform("useHDR", this->useHDR);
	shader->setUniform("usePBR", usePBR);
	shader->setUniform("u_useReflections", this->useReflections);
	if (this->useSSAO)
		shader->setUniform("u_SSAO_texture",(this->useSSAOBlur)? this->ssao_fbo->color_textures[1]:this->ssao_fbo->color_textures[0], 5);
	if (probe && isRenderingReflections)
		shader->setUniform("u_skybox_texture", probe->texture, 6);
	else
		shader->setUniform("u_skybox_texture", reflection, 6);

	//pass the inverse projection of the camera to reconstruct world pos.
	
	shader->setUniform("u_inverse_viewprojection", inv_vp);
	//pass the inverse window resolution, this may be useful
	shader->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));
	this->shadowMapAtlas->uploadDataToShader(shader,this->lights);
	/*
	if (this->isOptimizedDeferred) {
		shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
		glEnable(GL_CULL_FACE);
	}*/
	
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	
	bool renderToSphere = false;

	if (!lights.size()) {
		shader->setUniform("u_ambient_light", Vector3());
		quad->render(GL_TRIANGLES);
		
	}else
		for (int i = 0; i < lights.size(); ++i)
		{
			
			LightEntity* light = lights[i];

			if (isOptimizedDeferred && !renderToSphere) {
				if ((int) light->light_type < (int) eLightType::DIRECTIONAL) {
					
					renderToSphere = true;
					shader->disable();
					shader= Shader::Get("deferred_opti");
					shader->enable();
					shader->setUniform("u_ambient_light",(i==0)? scene->ambient_light:Vector3());
					glEnable(GL_CULL_FACE);
					
					shader->setUniform("u_use_normalmap", this->useNormalMap);
					shader->setFloat("u_useOcclusion", this->useOcclusion);
					shader->setUniform("u_gb0_texture", gbuffers_fbo->color_textures[0], 0);
					shader->setUniform("u_gb1_texture", gbuffers_fbo->color_textures[1], 1);
					shader->setUniform("u_gb2_texture", gbuffers_fbo->color_textures[2], 2);
					shader->setUniform("u_gb3_texture", gbuffers_fbo->color_textures[3], 3);
					shader->setUniform("u_depth_texture", gbuffers_fbo->depth_texture, 4);
					
					shader->setUniform("u_useReflections", false);
					shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
					shader->setUniform("u_camera_position", camera->eye);
					shader->setUniform("u_inverse_viewprojection", inv_vp);
					shader->setUniform("useHDR", this->useHDR);
					shader->setUniform("usePBR", usePBR);
					//pass the inverse window resolution, this may be useful
					shader->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));
					this->shadowMapAtlas->uploadDataToShader(shader, this->lights);
					glDisable(GL_DEPTH_TEST);
					glDisable(GL_BLEND);

					

				}
				
			}
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
			shader->setUniform("convertToGamma", (!this->useTonemapper) &&this->useHDR);
			
			uploadSingleLightToShader(shader, light);
			shader->setUniform("light_index", i);
			
			if (renderToSphere) {
				glFrontFace(GL_CW);
				sphere->render(GL_TRIANGLES);
				glFrontFace(GL_CCW);
			}else
				quad->render(GL_TRIANGLES);
			
			shader->setUniform("u_ambient_light", Vector3());
			
		}
		
	shader->disable();

	if (irr_probe_texture && useIrr) {
		Shader* ishader= Shader::Get("irradiance");
		ishader->enable();
		ishader->setUniform("u_inverse_viewprojection", inv_vp);
		ishader->setUniform("u_inv_view_matrix", inv_view);
		ishader->setUniform("u_gb0_texture", gbuffers_fbo->color_textures[0], 0);
		ishader->setUniform("u_gb1_texture", gbuffers_fbo->color_textures[1], 1);
		ishader->setUniform("u_gb2_texture", gbuffers_fbo->color_textures[2], 2);
		ishader->setUniform("u_depth_texture", gbuffers_fbo->depth_texture, 3);
		if(this->useSSAO)
			ishader->setUniform("u_SSAO_texture", (this->useSSAOBlur) ? this->ssao_fbo->color_textures[1] : this->ssao_fbo->color_textures[0], 4);
		else
			ishader->setUniform("u_SSAO_texture", Texture::getWhiteTexture(), 4);
		ishader->setUniform("u_probes_texture", irr_probe_texture, 5);
		
		ishader->setUniform("u_iRes", Vector2(1.0 / (float)width, 1.0 / (float)height));
		ishader->setUniform("u_irr_start", start_irr);
		ishader->setUniform("u_irr_end", end_irr);
		ishader->setUniform("u_irr_dims", irr_probe_dim);
		ishader->setUniform("u_num_probes", irr_probe_texture->height);
		ishader->setUniform("u_irr_normal_distance",.1f );
		ishader->setUniform("u_irr_delta", end_irr - start_irr);
		ishader->setUniform("multiplier", irrMultiplier);


		quad->render(GL_TRIANGLES);
		
	}
	
	illumination_fbo->unbind();
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	
	
	//Apply multipass reading to gbuffers

	//Forward alpha render
	

	if (useTonemapper && useHDR) {
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		Shader* shaderT = Shader::Get("tonemapper");
		glDisable(GL_DEPTH_TEST);
		tonemapper_fbo->bind();
		shaderT->enable();
		

		shaderT->setUniform("u_texture", this->illumination_fbo->color_textures[0], 0);
		shaderT->setUniform("u_scale", this->u_scale);
		shaderT->setUniform("u_average_lum", this->u_average_lum);
		shaderT->setUniform("u_lumwhite2", this->u_lumwhite2);

		quad->render(GL_TRIANGLES);
		
		
		shaderT->disable();
		tonemapper_fbo->unbind();
		glEnable(GL_DEPTH_TEST);
		tonemapper_fbo->color_textures[0]->toViewport();
	}
	else
		illumination_fbo->color_textures[0]->toViewport();
	
	
	gbuffers_fbo->depth_texture->copyTo(0);
	
	
	
	
	
	
	
	
	glEnable(GL_DEPTH_TEST);
	for (int i = 0; i < alphaNodes.size(); ++i) {
		RenderCall* rc = alphaNodes[i];
		//BoundingBox world_bounding = transformBoundingBox(rc.model, rc.mesh->box);
		renderMeshWithMaterialAndLighting(rc->model, rc->mesh, rc->material, camera);
	}

	
	if (this->useVolumetric) {
		glDisable(GL_DEPTH_TEST);
		volumetric_fbo->bind();
		
		glClearColor(0,0,0, 1.0);
		shader = Shader::Get("volumetric");
		shader->enable();
		shader->setUniform("u_inverse_viewprojection", inv_vp);
		shader->setUniform("u_camera_position", camera->eye);
		shader->setUniform("u_depth_texture", gbuffers_fbo->depth_texture, 0);
		shader->setUniform("u_air_density", airDensity);
		shader->setUniform("u_iRes", Vector2(1.0 / (float)volumetric_fbo->color_textures[0]->width, 1.0 / (float)volumetric_fbo->color_textures[0]->height));
		glDisable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		this->shadowMapAtlas->uploadDataToShader(shader, this->lights);
		//Put in a for and get 
		for (int i = 0; i < lights.size(); ++i) {
			if (lights[i]->light_type == eLightType::POINT) continue;
			uploadSingleLightToShader(shader, lights[i]);
			shader->setUniform("light_index", i);
			quad->render(GL_TRIANGLES);
			if(i==0)
				glEnable(GL_BLEND);
			
			
			
		
		}
		//glDisable(GL_BLEND);
		shader->disable();
		volumetric_fbo->unbind();
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		
		volumetric_fbo->color_textures[0]->toViewport();
		glDisable(GL_BLEND);

		glBlendFunc(GL_ONE, GL_ONE);

	}

	if (useDoF) {
		DoF_fbo->bind();
		//Blur Pass;
		glClear(GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		//glClearColor(1, 0, 0, 1.0);
		shader = Shader::Get("blur");
		shader->enable();
		//glDisable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
		shader->setUniform("u_inverse_viewprojection", inv_vp);
		shader->setUniform("colTexture", (useTonemapper && useHDR)?tonemapper_fbo->color_textures[0]:illumination_fbo->color_textures[0], 0);
		shader->setUniform("u_iRes", Vector2(1.0 / (float)DoF_fbo->color_textures[0]->width,1.0 / (float)DoF_fbo->color_textures[0]->height));
		shader->setUniform("parameters",Vector2(4.0, 1));
		quad->render(GL_TRIANGLES);
		
		shader->disable();
		glDisable(GL_DEPTH_TEST);

		//DoF Pass
		shader = Shader::Get("depthOfField");
		shader->enable();
		shader->setUniform("u_iRes", Vector2(1.0 / (float)DoF_fbo->color_textures[0]->width, 1.0 / (float)DoF_fbo->color_textures[0]->height));
		shader->setUniform("u_inverse_viewprojection", inv_vp);
		shader->setUniform("u_depth_texture", gbuffers_fbo->depth_texture, 0);
		shader->setUniform("focusTexture", (useTonemapper && useHDR) ? tonemapper_fbo->color_textures[0] : illumination_fbo->color_textures[0], 1);
		shader->setUniform("outOfFocusTexture", DoF_fbo->color_textures[1],2);
		shader->setUniform("focusPoint", DoF_focusPoint);
		shader->setUniform("nearFar", Vector2(camera->near_plane,camera->far_plane));
		shader->setUniform("u_minDistance", DoF_minDist);
		shader->setUniform("u_maxDistance", DoF_maxDist);
		
		quad->render(GL_TRIANGLES);
		shader->disable();
		DoF_fbo->unbind();
		glDisable(GL_BLEND);
		DoF_fbo->color_textures[1]->toViewport();
	}

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

	if (showSSAO) {
		if (this->useSSAOBlur)
			ssao_fbo->color_textures[1]->toViewport();
		else
			ssao_fbo->color_textures[0]->toViewport();
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
	if (textureEmissive == NULL) textureEmissive = Texture::getWhiteTexture(); //a 1x1 white texture
	if (textureMRT == NULL) {
		//textureMRT = Texture::getWhiteTexture(); //a 1x1 white texture
	}
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
	
	shader->setUniform("useHDR", this->useHDR);
	shader->setFloat("u_emissive_factor", this->useEmissive ? 1.0 : 0.0);
	shader->setUniform("u_use_normalmap", this->useNormalMap);
	shader->setFloat("u_useOcclusion", this->useOcclusion);
	shader->setUniform("usePBR", usePBR);
	shader->setVector3("u_emmisive_mat_factor", material->emissive_factor);
	shader->setFloat("u_roughness_mat_factor", material->roughness_factor);
	shader->setFloat("u_metallic_mat_factor", material->metallic_factor);
	
	float t = getTime();
	shader->setUniform("u_time", t);

	shader->setUniform("u_color", material->color);
	if (texture)
		shader->setUniform("u_texture", texture, 0);

	if (textureEmissive)
		shader->setUniform("u_emissive_texture", textureEmissive, 1);
	if (textureMRT) {
		shader->setUniform("u_metallic_roughness_texture", textureMRT, 2);
		shader->setUniform("u_has_MRT_texture", true);
	}else
		shader->setUniform("u_has_MRT_texture", false);
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
	shader->setUniform("u_light_intensity", light->intensity);
	shader->setUniform("u_light_position", light->model.getTranslation());
	shader->setUniform("u_light_max_distance", light->max_distance);
	shader->setUniform("u_light_type", (int)light->light_type);
	shader->setUniform("u_light_vector", light->lightDirection);
	shader->setUniform("u_spotCosineCuttof", 0.0f);
	shader->setUniform("u_cone_angle", 0.0f);
	shader->setUniform("u_light_cast_shadows", false);
	shader->setUniform("usePBR", usePBR);
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
		textureEmissive = Texture::getWhiteTexture(); //a 1x1 white texture
	if (textureMRT == NULL) {
		
		//textureMRT = Texture::getWhiteTexture(); //a 1x1 white texture
	}
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
	shader->setUniform("u_useReflections", this->useReflections);
	
	shader->setUniform("u_model", model );
	
	shader->setFloat("u_emissive_factor", this->useEmissive?1.0:0.0 );

	shader->setVector3("u_emmisive_mat_factor", material->emissive_factor);
	shader->setFloat("u_roughness_mat_factor", material->roughness_factor);
	shader->setFloat("u_metallic_mat_factor", material->metallic_factor);
	
	shader->setUniform("u_use_normalmap", this->useNormalMap);
	shader->setFloat("u_useOcclusion", this->useOcclusion);
	shader->setUniform("usePBR", usePBR);
	float t = getTime();
	shader->setUniform("u_time", t );

	shader->setUniform("u_color", material->color);
	if(texture)
		shader->setUniform("u_texture", texture, 0);
	
	if (textureEmissive)
		shader->setUniform("u_emissive_texture", textureEmissive, 1);
	if (textureMRT) {
		shader->setUniform("u_metallic_roughness_texture", textureMRT, 2);
		shader->setUniform("u_has_MRT_texture",true);
	}
	else {
		shader->setUniform("u_has_MRT_texture",false);
	}
	if (textureNormal)
		shader->setUniform("u_normal_texture", textureNormal, 3);
	

	//this is used to say which is the alpha threshold to what we should not paint a pixel on the screen (to cut polygons according to texture alpha)
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);
	
	Texture* reflection = skybox;
	if(probe && isRenderingReflections)
		shader->setUniform("u_skybox_texture", probe->texture, 4);
	else
		shader->setUniform("u_skybox_texture", reflection, 4);
	shader->setUniform("u_has_reflection", isRenderingReflections);
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
		shader->setUniform("usePBR", usePBR);
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

void GTR::Renderer::updateReflectionProbes(GTR::Scene* scene) {
	for (int i = 0; i < scene->entities.size(); ++i) {
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible || ent->entity_type != eEntityType::REFLECTION_PROBE) continue;
		ReflectionProbeEntity* probe = (ReflectionProbeEntity*)ent;
		if (!probe->texture) {
			probe->texture = new Texture();
			probe->texture->createCubemap(256, 256,NULL,GL_RGB,GL_UNSIGNED_INT,true);
		}
		captureReflectionProbe(scene, probe->texture, probe->model.getTranslation());
		this->probe = probe;
	}
}

void GTR::Renderer::renderReflectionProbes(GTR::Scene* scene, Camera* camera) {
	Mesh* mesh = Mesh::Get("data/meshes/sphere.obj");
	Shader* shader = Shader::Get("reflectionProbe");
	shader->enable();

	
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_camera_position", camera->eye);
	
	
	for (int i = 0; i < scene->entities.size(); ++i) {
		BaseEntity* ent = scene->entities[i];
		if (!ent->visible || ent->entity_type != eEntityType::REFLECTION_PROBE) continue;
		ReflectionProbeEntity* probe = (ReflectionProbeEntity*)ent;
		if (!probe->texture) continue;
		
		shader->setUniform("u_model", ent->model);
		shader->setTexture("u_texture", probe->texture, 0);
		mesh->render(GL_TRIANGLES);
	}
	
	

	shader->disable();
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
}

void GTR::Renderer::captureReflectionProbe(GTR::Scene* scene, Texture* tex, Vector3 pos) {
	for (int i = 0; i < 6; ++i) {
		FBO* global_fbo= Texture::getGlobalFBO(tex,i);
		Camera camera;
		camera.setPerspective(90, 1, .1, 1000);
		Vector3 eye = pos;
		Vector3 center = pos + cubemapFaceNormals[i][2];
		Vector3 up = cubemapFaceNormals[i][1];
		camera.lookAt(eye, center, up);
		camera.enable();
		global_fbo->bind();
		this->isRenderingReflections = true;
		RenderForward(&camera, scene);
		this->isRenderingReflections = false;
		global_fbo->unbind();

		tex->generateMipmaps();

		
	}
}



std::vector<Vector3> GTR::generateSpherePoints(int num, float radius, bool hemi)
{
		std::vector<Vector3> points;
		points.resize(num);
		for (int i = 0; i < num; ++i)
		{
			Vector3& p = points[i];
			float u = random();
			float v = random();
			float theta = u * 2.0 * PI;
			float phi = acos(2.0 * v - 1.0);
			float r = cbrt(random() * 0.9 + 0.1) * radius;
			float sinTheta = sin(theta);
			float cosTheta = cos(theta);
			float sinPhi = sin(phi);
			float cosPhi = cos(phi);
			p.x = r * sinPhi * cosTheta;
			p.y = r * sinPhi * sinTheta;
			p.z = r * cosPhi;
			if (hemi && p.z < 0)
				p.z *= -1.0;
		}
		return points;
	
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
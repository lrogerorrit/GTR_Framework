#include "shadowAtlas.h"
#include "fbo.h"
#include "texture.h"
#include "shader.h"
#include "renderer.h"
#include "application.h"
#include "mesh.h"



GTR::shadowAtlas::shadowAtlas()
{
	atlasFBO = new FBO();
	atlasFBO->setDepthOnly(this->textureSize,this->textureSize);
	this->dataArray.reserve(this->maxLights);
	
	//atlasTexture = new Texture();
}

GTR::shadowAtlas::~shadowAtlas()
{
	
	delete[] this->atlasFBO;
	this->dataArray.clear();
	
	
	
}


Vector2 GTR::shadowAtlas::getTilePosition(int index)
{
	
	if (index == 0) return Vector2(0, 2048);
	if (index==1) return Vector2(0, 0);
	if (index==2) return Vector2(2048, 2048);
	if (index==3) return Vector2(2048, 1024);
	if (index==4) return Vector2(2048, 0);
	if (index == 5) return Vector2(3072, 1024);
	if (index == 6) return Vector2(3072, 0);
}

int GTR::shadowAtlas::getTileSize(int index)
{
	if (index<=2)
		return 2048;
	else
		return 1024;
}


void GTR::shadowAtlas::clearArray()
{
	for (shadowData& data : this->dataArray) {
		data.light->has_shadow_map = false;
		data.light->shadowAtlasIndex = -1;
	}
	
	this->dataArray.clear();
	this->lightNum = 0;
}

void GTR::shadowAtlas::addLight(LightEntity* light)
{
	shadowData lightData= shadowData();
	lightData.light = light;
	lightData.pos = this->getTilePosition(this->lightNum);
	lightData.shadowDimensions= this->getTileSize(this->lightNum);
	light->shadowAtlasIndex = this->lightNum;
	this->dataArray.push_back(lightData);
	this->lightNum++;
}

shadowData GTR::shadowAtlas::getData(int index)
{
	return this->dataArray[index];
}

shadowData GTR::shadowAtlas::getData(GTR::LightEntity* light)
{
	for (int i = 0; i < this->dataArray.size(); i++)
		if (this->dataArray[i].light == light)
			return this->dataArray[i];
}




void GTR::shadowAtlas::uploadDataToShader(Shader* shader,std::vector<LightEntity*>& lights)
{
	const int maxL = MAX_ATLAS_LIGHTS;
	
	Vector3 dataToSend[maxL] = {};
	const int numOfLights = lights.size();
	
	for (int i = 0; i < numOfLights; ++i) {
		int ind= lights[i]->shadowAtlasIndex;
		if (ind != -1)			
			dataToSend[i] = Vector3((getTilePosition(ind) / (float)this->textureSize), getTileSize(ind) / (float)this->textureSize);
	}
		
	//print of dataToSend to console

	shader->setUniform3Array("shadowMapInfo", (float*)&dataToSend, numOfLights);
	shader->setTexture("shadowAtlasTexture", this->atlasFBO->depth_texture, 8);	
}

void GTR::shadowAtlas::displayDepthToViewport( int size)
{
	Shader* shader = Shader::Get("depth");
	
	//get datshadow
	
	shader->enable();
	
	glViewport(Application::instance->window_width - size, 0,size, size);
	glDisable(GL_DEPTH_TEST);
	if (this->lightNum > 1) {
		glEnable(GL_SCISSOR_TEST);
	}
	
	for (int i = 0; i < this->lightNum; ++i) {
		shadowData data = this->getData(i);
		shader->setUniform("u_camera_nearfar", Vector2(data.light->shadow_cam->near_plane, data.light->shadow_cam->far_plane));
		glScissor(data.pos.x, data.pos.y, data.shadowDimensions, data.shadowDimensions);
		
		
		this->atlasFBO->depth_texture->toViewport(shader);
			
	}
	glDisable(GL_SCISSOR_TEST);
	glViewport(0, 0, Application::instance->window_width, Application::instance->window_height);
	shader->disable();
}


void renderFlatMesh(const Matrix44 model, Mesh* mesh, GTR::Material* material, Camera* camera,Vector3 data) {
	if (!mesh || !mesh->getNumVertices() || !material)
		return;
	assert(glGetError() == GL_NO_ERROR);
	Shader* shader = Shader::Get("flat");
	shader->enable();

	
		glEnable(GL_CULL_FACE);
	
	assert(glGetError() == GL_NO_ERROR);
	glViewport(data.x, data.y, data.z, data.z);
	shader->setUniform("u_viewprojection", camera->viewprojection_matrix);
	shader->setUniform("u_model", model);
	shader->setUniform("u_alpha_cutoff", material->alpha_mode == GTR::eAlphaMode::MASK ? material->alpha_cutoff : 0);

	glDepthFunc(GL_LESS);
	glDisable(GL_BLEND);
	mesh->render(GL_TRIANGLES);
	shader->disable();
}

void GTR::shadowAtlas::calculateShadows(std::vector<RenderCall>& renderCalls)
{
	Camera* view_cam = Camera::current;
	this->atlasFBO->bind();
	glColorMask(0, 0, 0, 0);
	glClear(GL_DEPTH_BUFFER_BIT);
	Shader* shader = Shader::Get("flat");
	int i_pos = 0;
	for (shadowData& data : this->dataArray){
		LightEntity* light = data.light;
		
		
		if (!light->shadow_cam)
			light->shadow_cam = new Camera();

		if (light->light_type == eLightType::DIRECTIONAL) {
			light->shadow_cam->setOrthographic(-light->area_size / 2, light->area_size / 2, light->area_size / 2, -light->area_size / 2, .1, light->max_distance);

			light->shadow_cam->lookAt(light->model.getTranslation(), light->model.getTranslation() - (light->lightDirection * 20), Vector3(0, 1, 0));
			float grid = (float) (light->area_size) / (float) getTileSize(i_pos);

		//snap camera X,Y to that size in camera space assuming the frustum is square, otherwise compute gridxand gridy
			light->shadow_cam->view_matrix.M[3][0] = round(light->shadow_cam->view_matrix.M[3][0] / grid) * grid;

			light->shadow_cam->view_matrix.M[3][1] = round(light->shadow_cam->view_matrix.M[3][1] / grid) * grid;

		//update viewproj matrix (be sure no one changes it)
			light->shadow_cam->viewprojection_matrix = light->shadow_cam->view_matrix * light->shadow_cam->projection_matrix;
			//light->shadow_cam->lookAt(light->model.getTranslation(), light->model.getTranslation() + light->lightDirection, Vector3(0, -1, 0));
		}
		else if (light->light_type == eLightType::SPOT) {
			light->shadow_cam->setPerspective(light->cone_angle*2, 1.0, 0.1, light->max_distance);
			light->shadow_cam->lookAt(light->model.getTranslation(), light->model * Vector3(0, 0, -1), light->model.rotateVector(Vector3(0, 1, 0)));
		}

		//compute texel size in world units, where frustum size is the distance from left to right in the camera
		

		
		i_pos++;
		light->shadow_cam->enable();

		for (RenderCall& rc : renderCalls){
			if (rc.material->alpha_mode == eAlphaMode::BLEND)
				continue;
			if (light->shadow_cam->testBoxInFrustum(rc.boundingBox.center, rc.boundingBox.halfsize))
				renderFlatMesh(rc.model, rc.mesh, rc.material, light->shadow_cam,Vector3(data.pos,data.shadowDimensions));
		};
		light->has_shadow_map = true;
	};
	shader->disable();
	//change viewport to original
	glViewport(0, 0, Application::instance->window_width, Application::instance->window_height);

	
	this->atlasFBO->unbind();
	glColorMask(1, 1, 1, 1);
	view_cam->enable();
}
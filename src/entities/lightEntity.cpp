#include "lightEntity.h"
#include "../scene.h"
#include "../utils.h"
#include "../shader.h"
#include "../application.h"





GTR::LightEntity::LightEntity() {
	entity_type = eEntityType::LIGHT;
	color.set(1, 1, 1);
	intensity = 1;
	max_distance = 100;
	cone_angle = 0.0f;
	cone_exp = .0f;
	cast_shadows = false;
	area_size = .0f;
	lightDirection.set(0,-1,0);
	shadow_bias = 0.0f;
	
	
	
	
	
	
}

void GTR::LightEntity::renderInMenu() {

	const char* type_str = light_type == eLightType::DIRECTIONAL ? "Directional" : (light_type == eLightType::POINT ? "Point" : "Spot");
	
	ImGui::Text("LightType: %s", type_str);
	ImGui::ColorEdit3("Color", color.v);
	ImGui::DragFloat("Intensity", &intensity,.1f);
	ImGui::DragFloat("Max Distance", &max_distance,1);
	ImGui::DragFloat("Shadow Bias", &shadow_bias,.01f);


	switch (light_type) {
		case (eLightType::SPOT): 
			ImGui::DragFloat("Cone Angle", &cone_angle,.1f);
			ImGui::DragFloat("Cone Exponent",&cone_exp,.1f);
			
			break;
		case (eLightType::DIRECTIONAL):
			ImGui::DragFloat("Area Size", &area_size);
			break;
	}
	ImGui::SliderFloat3("Light Direction", lightDirection.v, -1, 1);
	
	
	ImGui::Checkbox("Cast shadows", &cast_shadows);
	
	
	//TODO
	BaseEntity::renderInMenu();
}

void GTR::LightEntity::configure(cJSON* json)
{
	intensity = readJSONNumber(json, "intensity", intensity);
	color = readJSONVector3(json, "color", color);
	std::string str=  readJSONString(json, "light_type", "");
	max_distance= readJSONNumber(json, "max_dist", max_distance);
	cone_angle = readJSONNumber(json,"cone_angle",cone_angle);
	cone_exp = readJSONNumber(json, "cone_exp", cone_exp);
	area_size= readJSONNumber(json, "area_size", area_size);
	target = readJSONVector3(json, "target", target);
	cast_shadows = readJSONBool(json, "cast_shadows", cast_shadows);
	shadow_bias = readJSONNumber(json, "shadow_bias", shadow_bias);

	
	
	if (str == "POINT")
		light_type = eLightType::POINT;
	else if (str == "SPOT") {
		light_type = eLightType::SPOT;
		this->lightDirection = this->model.frontVector();
		
	}
	else if (str == "DIRECTIONAL") {
		light_type = eLightType::DIRECTIONAL;
		this->lightDirection = (this->model.getTranslation() - this->target).normalize();
	}
	
	if (cast_shadows) {
		this->shadow_cam = new Camera();
		if (this->light_type == eLightType::DIRECTIONAL) {
			//shadow_cam->setOrthographic(this->area_size / 2, this->area_size / 2, this->area_size / 2, this->area_size / 2, .1, this->max_distance);
			//shadow_cam->setPerspective(cone_angle, 1.0, 0.1, max_distance);
			//shadow_cam->lookAt(scene->main_camera.eye * this->model.frontVector() * -50, scene->main_camera.center, scene->main_camera.up);
			
			//shadow_cam->lookAt(this->model.getTranslation(), this->model * this->lightDirection.normalize() * Vector3(0, 0, 1), this->lightDirection.normalize() * Vector3(0, -1, 0));
		}
		else {
			shadow_cam->setPerspective(cone_angle, 1.0, 0.1, max_distance);
			shadow_cam->lookAt(this->model.getTranslation(),this->model*Vector3(0,0,1),this->model.rotateVector(Vector3(0,1,0)));
		}
	}

}



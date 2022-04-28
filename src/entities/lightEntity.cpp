#include "lightEntity.h"
#include "../scene.h"
#include "../utils.h"





GTR::LightEntity::LightEntity() {
	entity_type = eEntityType::LIGHT;
	color.set(1, 1, 1);
	intensity = 1;
	max_distance = 100;
	cone_angle = 45;
	cone_exp = 100;
	cast_shadows = false;
	area_size = 1.5;
	lightDirection.set(0,-1,0);
	
	
	
	
}

void GTR::LightEntity::renderInMenu() {

	const char* type_str = light_type == eLightType::DIRECTIONAL ? "Directional" : (light_type == eLightType::POINT ? "Point" : "Spot");
	
	ImGui::Text("LightType: %s", type_str);
	ImGui::ColorEdit3("Color", color.v);
	ImGui::DragFloat("Intensity", &intensity,.1f);
	ImGui::DragFloat("Max Distance", &max_distance,1);
	
	switch (light_type) {
		case (eLightType::SPOT): 
			ImGui::DragFloat("Cone Angle", &cone_angle,.1f);
			ImGui::DragFloat("Cone Exponent",&cone_exp,.1f);
			
			break;
	}
	ImGui::SliderFloat3("Light Direction", lightDirection.v, -1, 1);
	
	
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
	cast_shadows = readJSONBool(json, "cast_shadows", false);

	
	
	if (str == "POINT")
		light_type = eLightType::POINT;
	else if (str == "SPOT") {
		light_type = eLightType::SPOT;
		this->lightDirection = this->model.frontVector();
		
	}
	else if (str == "DIRECTIONAL")
		light_type = eLightType::DIRECTIONAL;
	
	if (cast_shadows) {
		this->shadow_cam = new Camera();
		if (this->light_type == eLightType::DIRECTIONAL) {
			shadow_cam->lookAt(scene->main_camera.eye * this->model.frontVector() * -50, scene->main_camera.center, scene->main_camera.up);
			shadow_cam->setOrthographic(this->area_size / 2, this->area_size / 2, this->area_size / 2, this->area_size / 2, 10, this->max_distance * .7);
		}
		else {
			shadow_cam->lookAt(this->model);
			shadow_cam->setPerspective(90, 1, 0.1, max_distance);
		}
	}

}



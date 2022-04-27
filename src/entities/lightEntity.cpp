#include "lightEntity.h"
#include "../scene.h"
#include "../utils.h"





GTR::LightEntity::LightEntity() {
	entity_type = eEntityType::LIGHT;
	color.set(1, 1, 1);
	intensity = 1;
	
}

void GTR::LightEntity::renderInMenu() {

	const char* type_str = light_type == eLightType::DIRECTIONAL ? "Directional" : (light_type == eLightType::POINT ? "Point" : "Spot");
	
	ImGui::Text("LightType: %s", type_str);
	ImGui::ColorEdit3("Color", color.v);
	ImGui::DragFloat("Intensity", &intensity,1.0f,0.0f);
	//TODO
	BaseEntity::renderInMenu();
}

void GTR::LightEntity::configure(cJSON* json)
{
	intensity = readJSONNumber(json, "intensity", intensity);
	color = readJSONVector3(json, "color", color);
	std::string str=  readJSONString(json, "light_type", "");
	if (str == "POINT")
		light_type = eLightType::POINT;
	else if (str == "SPOT")
		light_type = eLightType::SPOT;
	else if (str == "DIRECTIONAL")
		light_type = eLightType::DIRECTIONAL;
	
}
#include "lightEntity.h"
#include "../scene.h"
#include "../utils.h"





GTR::LightEntity::LightEntity() {
	entity_type = eEntityType::LIGHT;
	color.set(1, 1, 1);
	intensity = 1;
	
}

void GTR::LightEntity::renderInMenu() {
	BaseEntity::renderInMenu();
	//TODO
}

void GTR::LightEntity::configure(cJSON* json)
{
	intensity = readJSONNumber(json, "intensity", intensity);
	color = readJSONVector3(json, "color", color);
}
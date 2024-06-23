// My first gd mod

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GameLevelManager.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/Loader.hpp>

#include <fstream>
#include <string>
#include <vector>

using namespace geode::prelude;

using DeathPoints = std::vector<CCPoint>;
static_assert(sizeof(float) == 4); // Floats have to be 4 bytes for file format (i dont think this will ever error but just to be sure)

const float MINIMUM_MARKER_SPEED = 0.1f; // minimum delay between each iteration when looping through visible markers

// This function has caused me so much pain and suffering
// For some reason robtop doesnt use a camera and instead translates the layer its self
bool shouldRender(CCPoint point, CCNode* parentNode) {
	auto winSize = CCDirector::get()->getWinSize();
	auto globPos = parentNode->convertToWorldSpace(point);
	
	// I have no clue how to get the camera rotation so this is my shitty solution for now
	float range = std::max(winSize.width, winSize.height) / 2;
	return globPos.getDistance(winSize/2) < range;

	//return globPos.x > 0 && globPos.y > 0 && globPos.x < winSize.width && globPos.y < winSize.height;
}

std::filesystem::path getFilePath(GJGameLevel* lvl) {
	auto path = Mod::get()->getSaveDir();
	int id = lvl->m_levelID.value();
	if (id == 0) { // id is 0 when playtesting editor levels
		//path /= lvl->m_levelName.c_str();
		path /= ("my" + std::to_string(lvl->m_M_ID)); // IDK WHAT M_ID IS BUT THIS SEEMS TO WORK FOR UNIQUE "MY LEVELS"
	} else {
		path /= std::to_string(id);
	}
	path += ".DI";
	return path;
}

// SUUUPER basic file format
// im probably gonna regret not adding a version to the file header but its too late now
// ^ I DEFINITELY DO :)
/*
struct DIPoint {
	float x, y;
};

struct DIFile {
	uint32_t length;
	DIPoint points[];
};
*/

DeathPoints loadDeathPoints(GJGameLevel* lvl) {
	auto path = getFilePath(lvl);

	// Return an empty array if file doesnt exist
	if (!std::filesystem::exists(path)) {
		return DeathPoints();
	}

	#define CHECKSTREAM(stmt) { stmt; if (!file.good()) { log::error("Failed to load DI file: {}", std::strerror(errno)); file.close(); return DeathPoints(); } }
	std::ifstream file;
	CHECKSTREAM(file.open(path, std::ios::binary));
		
	// Get length
	uint32_t length;
	CHECKSTREAM(file.read((char*)&length, sizeof(length)));

	constexpr size_t pointSize = sizeof(float)*2; // NOT CCPoint
	DeathPoints points;
	points.reserve(length);

	for (size_t i = 0; i < length*pointSize; i += pointSize) {
		float x, y;
		CHECKSTREAM(file.read((char*)&x, sizeof(float))); 
		CHECKSTREAM(file.read((char*)&y, sizeof(float)));
		points.emplace_back(x, y);
	}

	file.close();
	return points;

	#undef CHECKSTREAM
}

void saveDeathPoints(GJGameLevel* lvl, const DeathPoints& deathPoints) {
	auto path = getFilePath(lvl);

	#define CHECKSTREAM(stmt) { stmt; if (!file.good()) { log::error("Failed to save DI file: {}", std::strerror(errno)); file.close(); return; } }
	std::ofstream file;
	CHECKSTREAM(file.open(path, std::ios::binary | std::ios::trunc));

	// Write length
	uint32_t length = deathPoints.size();
	CHECKSTREAM(file.write((char*)&length, sizeof(uint32_t)));

	for (auto point : deathPoints) {
		CHECKSTREAM(file.write((char*)&point.x, sizeof(float)));
		CHECKSTREAM(file.write((char*)&point.y, sizeof(float)));
	}

	file.close();
	#undef CHECKSTREAM
}

bool isEnabled() {
	PlayLayer* playLayer = PlayLayer::get();
	if (!playLayer) { // Safety
		return false; 
	}

	if (!Mod::get()->getSettingValue<bool>("enable-markers")) return false;

	if (playLayer->m_level->isPlatformer()) {
		if (!Mod::get()->getSettingValue<bool>("show-platformer")) return false;
	} else {
		if (!Mod::get()->getSettingValue<bool>("show-classic")) return false;
	}

	if (playLayer->m_isPracticeMode) {
		if (!Mod::get()->getSettingValue<bool>("show-practice")) return false;
	} else if (playLayer->m_isTestMode) {
		if (!Mod::get()->getSettingValue<bool>("show-testmode")) return false;
	} else {
		if (!Mod::get()->getSettingValue<bool>("show-normal")) return false;
	}

	return true;
}

class $modify(ModifiedPlayLayer, PlayLayer) {
	struct Fields {
		DeathPoints m_deathPoints;
		CCNode* m_deathSprites;
		float m_respawnTimeSum; // Hacky way to calc respawn time
		float m_deathMarkerAnimTime;
	};

	bool init(GJGameLevel* p0, bool p1, bool p2) {
		m_fields->m_deathSprites = CCNode::create();

		if (!PlayLayer::init(p0, p1, p2)) return false;

		m_fields->m_deathSprites->setZOrder(999999999);
		this->m_objectLayer->addChild(m_fields->m_deathSprites);
		m_fields->m_deathPoints = loadDeathPoints(this->m_level);

		return true;
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		m_fields->m_deathSprites->removeAllChildrenWithCleanup(true);
		m_fields->m_deathSprites->cleanup();
	}

	void onQuit() {
		PlayLayer::onQuit();
		saveDeathPoints(this->m_level, m_fields->m_deathPoints);
	}

	void delayedResetLevel() { // Override the delayed reset
		if (!isEnabled()) {
			PlayLayer::delayedResetLevel();
			return;
		}

		if (m_fields->m_deathMarkerAnimTime > m_fields->m_respawnTimeSum) {
			m_fields->m_deathSprites->runAction(CCSequence::createWithTwoActions(
				CCDelayTime::create(m_fields->m_deathMarkerAnimTime - m_fields->m_respawnTimeSum),
				CCCallFunc::create(this, callfunc_selector(PlayLayer::delayedResetLevel))
			));
		} else {
			PlayLayer::delayedResetLevel();
		}
	}

	void updateCalcRespawnTime() {
		m_fields->m_respawnTimeSum += CCDirector::get()->getDeltaTime();
	}
};

class $modify(ModifiedPlayerObject, PlayerObject) {
	static void onModify(auto& self) {
		// Hook before QOLMod (-6969) hook that completely overrides playerDestroyed
		if(!self.setHookPriority("PlayerObject::playerDestroyed", -6970)) {
			log::error("Failed to set priority of PlayerObject::playerDestroyed to -6970 (somehow)");
		}
	}

	void playerDestroyed(bool secondPlr) {
		PlayerObject::playerDestroyed(secondPlr);

		if (secondPlr) {
			return;
		}

		if (!isEnabled()) {
			return;
		}

		auto game = GameManager::get();
		auto mod = Mod::get();
		auto playLayer = static_cast<ModifiedPlayLayer*>(game->getPlayLayer());
		if (playLayer) { // If player died in play layer not in the editor or main menu
			playLayer->m_fields->m_respawnTimeSum = 0; // super hacky
			playLayer->m_fields->m_deathSprites->runAction(
				CCRepeatForever::create(CCSequence::createWithTwoActions(
					CCDelayTime::create(0), 
					CCCallFunc::create(playLayer, callfunc_selector(ModifiedPlayLayer::updateCalcRespawnTime)))
				)
			);

			auto& deathPoints = playLayer->m_fields->m_deathPoints;
			auto deathSprites = playLayer->m_fields->m_deathSprites;
			deathPoints.push_back(this->getPosition());
			
			float& totalTime = playLayer->m_fields->m_deathMarkerAnimTime;
			totalTime = 0;

			bool animateMarkers = mod->getSettingValue<bool>("animate-markers");
			double markerScale = mod->getSettingValue<double>("marker-scale");
			double markerScaleThisDeath = markerScale * mod->getSettingValue<double>("marker-scale-thisdeath");

			DeathPoints visiblePoints; visiblePoints.reserve(deathPoints.size());
			int thisDeathIdx = -1;
			for (auto iter = deathPoints.rbegin(); iter != deathPoints.rend(); iter++) {
				auto& point = *iter;
				if (shouldRender(point, deathSprites)) {
					if (std::distance(iter, deathPoints.rbegin()) == 0) {
						thisDeathIdx = 0;
					}

					visiblePoints.push_back(point);
				}
			}

			int idx = 0;
			float interval = mod->getSettingValue<double>("marker-anim-time") / visiblePoints.size();
			for (auto& point : visiblePoints) {
				auto sprite = CCSprite::create("death-marker.png"_spr);
				sprite->setScale(markerScale);
				sprite->setAnchorPoint({0.5f, 0.0f});

				if (idx == thisDeathIdx) {
					sprite->setZOrder(99999);
					sprite->setScale(markerScaleThisDeath);
				}

				if (animateMarkers)
				{
					sprite->setPosition(point + CCPoint(0.0f, 20.0f));
					sprite->setOpacity(0);
					sprite->runAction(CCSequence::createWithTwoActions(
						CCDelayTime::create(idx * interval),
						CCSpawn::createWithTwoActions(
							CCMoveTo::create(0.25f, point),
							CCFadeIn::create(0.25f)
						)
					));
					totalTime += interval;
				} else {
					sprite->setPosition(point);
				}

				deathSprites->addChild(sprite);
				idx++;
			}

			totalTime += mod->getSettingValue<double>("marker-time");
		}
	}
};

class $modify(GameLevelManager) {
	void deleteLevel(GJGameLevel* lvl) {
		auto path = getFilePath(lvl);
		GameLevelManager::deleteLevel(lvl);
		std::filesystem::remove(path);
	}
};

class $modify(ModifiedPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();
		if (Mod::get()->getSettingValue<bool>("pause-menu-options") && Loader::get()->isModLoaded("geode.node-ids")) {
			auto sprite = CCSprite::createWithSpriteFrameName("GJ_plainBtn_001.png");
			sprite->setScale(0.6f);

			auto icon = CCSprite::create("death-marker.png"_spr);
			icon->setScale(0.95f);
			icon->setPosition(sprite->getContentSize()/2);
			sprite->addChild(icon);
			
			auto button = CCMenuItemSpriteExtra::create(
				sprite, 
				this, 
				menu_selector(ModifiedPauseLayer::openDISettings)
			);

			auto menu = this->getChildByID("right-button-menu");
			menu->addChild(button);
			menu->updateLayout();
		}
	}	

	void openDISettings(CCObject*) {
		geode::openSettingsPopup(Mod::get());
	}
};
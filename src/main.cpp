// My first gd mod
// I dont usually make shit in C++ so sorry if this code is terrible (im a rust guy)

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GameLevelManager.hpp>

#include <fstream>
#include <string>
#include <vector>

using namespace geode::prelude;

using DeathPoints = std::vector<CCPoint>;
static_assert(sizeof(float) == 4); // Floats have to be 4 bytes for file format (i dont think this will ever error but just to be sure)

bool shouldRender(CCPoint point, PlayerObject* plr) {
	CCSize winSize = CCDirector::get()->getWinSize()/2;
	float zoom = PlayLayer::get()->getScale();
	float range = std::max(winSize.width, winSize.height);
	return (point.getDistance(plr->getPosition()) / zoom) < range;
}

ghc::filesystem::path getFilePath(GJGameLevel* lvl) {
	auto path = Mod::get()->getSaveDir();
	int id = lvl->m_levelID.value();
	if (id == 0) { // id is 0 when playtesting editor levels so just use level name since those have to be unique anyways
		//path /= lvl->m_levelName.c_str();
		path /= ("my" + std::to_string(lvl->m_M_ID)); // IDK WHAT M_ID IS BUT THIS SEEMS TO WORK FOR UNIQUE "MY LEVELS"
	} else {
		path /= std::to_string(id);
	}
	path += ".DI";
	return path;
}

// SUUUPER basic file format
/*
struct DIPoint {
	float x, y;
}

struct DIFile {
	uint32_t length;
	DIPoint points[];
};
*/

DeathPoints loadDeathPoints(GJGameLevel* lvl) {
	auto path = getFilePath(lvl);

	// Return an empty array if file doesnt exist
	if (!ghc::filesystem::exists(path)) {
		return DeathPoints();
	}

	std::ifstream file;
	file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	try {
		file.open(path, std::ios::binary);
		
		// Get length
		uint32_t length;
		file.read((char*)&length, sizeof(length));

		constexpr size_t pointSize = sizeof(float)*2; // NOT CCPoint
		DeathPoints points;
		points.reserve(length);

		for (size_t i = 0; i < length*pointSize; i += pointSize) {
			float x, y;
			file.read((char*)&x, sizeof(float));
			file.read((char*)&y, sizeof(float));
			points.emplace_back(x, y);
		}

		file.close();
		return points;
	} catch (std::ifstream::failure e) {
		log::error("Failed to load DI file: {}", e.what());
		return DeathPoints(); // return an empty array
	}
}

void saveDeathPoints(GJGameLevel* lvl, const DeathPoints& deathPoints) {
	auto path = getFilePath(lvl);

	std::ofstream file;
	file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	try {
		file.open(path, std::ios::binary | std::ios::trunc);
		
		// Write length
		uint32_t length = deathPoints.size();
		file.write((char*)&length, sizeof(uint32_t));

		for (auto point : deathPoints) {
			file.write((char*)&point.x, sizeof(float));
			file.write((char*)&point.y, sizeof(float));
		}

		file.close();
	} catch (std::ifstream::failure e) {
		log::error("Failed to save DI file: {}", e.what());
	}
}

bool isEnabled() {
	PlayLayer* playLayer = PlayLayer::get();
	if (!playLayer) { // Safety
		return false; 
	}

	if (playLayer->m_isPracticeMode && !Mod::get()->getSettingValue<bool>("show-practice")) {
		return false;
	}

	// Until m_isTestMode is in the bindings this is how im gonna do this
	// This probably isnt the best way of doing this and will probably break next update
	if (*(bool*)(&playLayer->m_isPracticeMode + 48) && !Mod::get()->getSettingValue<bool>("show-testmode")) {
		return false;
	}

	return Mod::get()->getSettingValue<bool>("enable-indicators");
}

class $modify(ModifiedPlayLayer, PlayLayer) {
	DeathPoints m_deathPoints;
	CCNode* m_deathSprites;

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
		/* 
		// since m_inTestMode isnt in the 2.2 GJBaseGameLayer bindings i have to brute force
		// (im a noobie and this is the only way i know how lmao)
		for (int i = 1; i <= 211; i++) {
			log::debug("tm + {}: {}", i, (*(bool*)(&m_isPracticeMode + i)) == true);
		}
		*/

		PlayLayer::onQuit();
		saveDeathPoints(this->m_level, m_fields->m_deathPoints);
	}

	void delayedResetLevel() { // Override the delayed reset
		if (!isEnabled()) {
			PlayLayer::delayedResetLevel();
		}
	}
};

class $modify(ModifiedPlayerObject, PlayerObject) {
	void playerDestroyed(bool p0) {
		PlayerObject::playerDestroyed(p0);

		if (!isEnabled()) {
			return;
		}

		auto game = GameManager::get();
		auto playLayer = static_cast<ModifiedPlayLayer*>(game->getPlayLayer());
		if (playLayer) { // If player died in play layer not in the editor or main menu
			auto& deathPoints = playLayer->m_fields->m_deathPoints;
			auto deathSprites = playLayer->m_fields->m_deathSprites;
			deathPoints.push_back(this->getPosition());
			
			int idx = 0;
			float totalTime = 0.0f;

			for (auto iter = deathPoints.rbegin(); iter != deathPoints.rend(); iter++) {
				auto point = *iter;
				bool thisDeath = idx == 0;

				if (shouldRender(point, this)) {
					auto sprite = CCSprite::create("death-indicator.png"_spr);
					sprite->setScale(thisDeath ? 0.8f : 0.5f);
					sprite->setAnchorPoint({0.5f, 0.0f});
					if (thisDeath) sprite->setZOrder(99999);

					// anim
					sprite->setPosition(point + CCPoint(0.0f, 20.0f));
					sprite->setOpacity(0);
					sprite->runAction(CCSequence::createWithTwoActions(
						CCDelayTime::create(idx * 0.01f),
						CCSpawn::createWithTwoActions(
							CCMoveTo::create(0.25f, point),
							CCFadeIn::create(0.25f)
						)
					));

					// anim end

					deathSprites->addChild(sprite);
					totalTime += 0.01f;
					idx++;
				}
			}

			totalTime += game->getGameVariable("0052") ? 1.0f : 2.0f; // Enable Faster Reset var
			log::info("DI TOTAL TIME: {}", totalTime);

			deathSprites->runAction(CCSequence::createWithTwoActions(
				CCDelayTime::create(totalTime),
				CCCallFunc::create(this, callfunc_selector(ModifiedPlayerObject::onDIFinish))
			));
		}
	}

	void onDIFinish() {
		auto playLayer = PlayLayer::get();
		if (playLayer) {
			playLayer->resetLevel();
		}
	}
};

class $modify(GameLevelManager) {
	void deleteLevel(GJGameLevel* lvl) {
		auto path = getFilePath(lvl);
		GameLevelManager::deleteLevel(lvl);
		ghc::filesystem::remove(path);
	}
};
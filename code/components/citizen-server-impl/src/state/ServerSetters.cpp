#include "StdInc.h"
#include <state/SyncTrees.h>

#ifndef STATE_RDR3
#include <state/ServerGameState.h>
#include <ScriptEngine.h>

#include <GameServer.h>

#include <Resource.h>
#include <fxScripting.h>

#include <boost/mpl/at.hpp>
#include <boost/function_types/parameter_types.hpp>
#include <json.hpp>

using json = nlohmann::json;

namespace fx
{
#pragma region helpers
template<typename TNode, typename TWrapper>
void UnparseTo(TNode& node, TWrapper wrapper)
{
	rl::MessageBuffer mb(wrapper->data.size());
	sync::SyncUnparseState state{ mb };
	node.Unparse(state);

	memcpy(wrapper->data.data(), mb.GetBuffer().data(), mb.GetBuffer().size());

	wrapper->length = mb.GetCurrentBit();
	wrapper->node = node;
}

template<typename TTree, typename TFn>
void SetupNode(const std::shared_ptr<TTree>& tree, TFn fn)
{
	using TArgs = typename boost::function_types::parameter_types<decltype(&TFn::operator())>::type;
	using TArg = typename boost::mpl::at_c<TArgs, 1>::type;
	using TNode = std::remove_reference_t<TArg>;

	auto n = tree->template GetNode<TNode>();
	fn(n->node);

	UnparseTo(n->node, n);

	n->frameIndex = 12;
	n->timestamp = msec().count();
}

template<typename TSectorNode, typename TPositionNode, typename TTree>
void SetupPosition(const std::shared_ptr<TTree>& tree, float posX, float posY, float posZ)
{
	int sectorX = int((posX / 54.0f) + 512.0f);
	int sectorY = int((posY / 54.0f) + 512.0f);
	int sectorZ = int((posZ + 1700.0f) / 69.0f);

	float sectorPosX = posX - ((sectorX - 512.0f) * 54.0f);
	float sectorPosY = posY - ((sectorY - 512.0f) * 54.0f);
	float sectorPosZ = posZ - ((sectorZ * 69.0f) - 1700.0f);

	SetupNode(tree, [sectorX, sectorY, sectorZ](TSectorNode& cdn)
	{
		cdn.m_sectorX = sectorX;
		cdn.m_sectorY = sectorY;
		cdn.m_sectorZ = sectorZ;
	});

	SetupNode(tree, [sectorPosX, sectorPosY, sectorPosZ](TPositionNode& cdn)
	{
		cdn.m_posX = sectorPosX;
		cdn.m_posY = sectorPosY;
		cdn.m_posZ = sectorPosZ;
	});
}

template<typename TTree>
void SetupHeading(const std::shared_ptr<TTree>& tree, float heading)
{
	SetupNode(tree, [heading](sync::CEntityOrientationDataNode& node)
	{
		glm::quat q = glm::quat(glm::vec3(0.0f, 0.0f, heading * 0.01745329252f));
		node.data.quat.Load(q.x, q.y, q.z, q.w);
	});
}
#pragma endregion

std::shared_ptr<sync::SyncTreeBase> MakeAutomobile(uint32_t model, float posX, float posY, float posZ, uint32_t resourceHash, float heading = 0.0f)
{
	auto tree = std::make_shared<sync::CAutomobileSyncTree>();
	
	SetupNode(tree, [model](sync::CVehicleCreationDataNode& cdn)
	{
		cdn.m_model = model;
		cdn.m_creationToken = msec().count();
		cdn.m_needsToBeHotwired = false;
		cdn.m_maxHealth = 1000;
		cdn.m_popType = sync::POPTYPE_MISSION;
		cdn.m_randomSeed = rand();
		cdn.m_tyresDontBurst = false;
		cdn.m_vehicleStatus = 2;
		cdn.m_usesSpecialFlightMode = false;
	});

	SetupNode(tree, [](sync::CAutomobileCreationDataNode& cdn)
	{
		cdn.allDoorsClosed = true;
	});

	SetupPosition<sync::CSectorDataNode, sync::CSectorPositionDataNode>(tree, posX, posY, posZ);
	SetupHeading(tree, heading);

	SetupNode(tree, [resourceHash](sync::CEntityScriptInfoDataNode& cdn)
	{
		cdn.m_scriptHash = resourceHash;
		cdn.m_timestamp = msec().count();
	});

	return tree;
}

template<typename TTree>
std::shared_ptr<sync::SyncTreeBase> MakeVehicle(uint32_t model, float posX, float posY, float posZ, uint32_t resourceHash, float heading = 0.0f)
{
	auto tree = std::make_shared<TTree>();

	SetupNode(tree, [model](sync::CVehicleCreationDataNode& cdn)
	{
		cdn.m_model = model;
		cdn.m_creationToken = msec().count();
		cdn.m_needsToBeHotwired = false;
		cdn.m_maxHealth = 1000;
		cdn.m_popType = sync::POPTYPE_MISSION;
		cdn.m_randomSeed = rand();
		cdn.m_tyresDontBurst = false;
		cdn.m_vehicleStatus = 2;
		cdn.m_usesSpecialFlightMode = false;
	});

	if constexpr (std::is_same_v<TTree, sync::CAutomobileSyncTree> ||
		std::is_same_v<TTree, sync::CTrailerSyncTree> ||
		std::is_same_v<TTree, sync::CHeliSyncTree>)
	{
		SetupNode(tree, [](sync::CAutomobileCreationDataNode& cdn)
		{
			cdn.allDoorsClosed = true;
		});
	}

	SetupPosition<sync::CSectorDataNode, sync::CSectorPositionDataNode>(tree, posX, posY, posZ);
	SetupHeading(tree, heading);

	SetupNode(tree, [resourceHash](sync::CEntityScriptInfoDataNode& cdn)
	{
		cdn.m_scriptHash = resourceHash;
		cdn.m_timestamp = msec().count();
	});

	return tree;
}

std::shared_ptr<sync::SyncTreeBase> MakePed(uint32_t model, float posX, float posY, float posZ, uint32_t resourceHash, float heading = 0.0f)
{
	auto tree = std::make_shared<sync::CPedSyncTree>();

	SetupNode(tree, [model](sync::CPedCreationDataNode& cdn)
	{
		cdn.m_model = model;
		cdn.isRespawnObjectId = false;
		cdn.respawnFlaggedForRemoval = false;
		cdn.m_popType = sync::POPTYPE_MISSION;
		cdn.randomSeed = rand();
		cdn.vehicleId = 0;
		cdn.vehicleSeat = 0;
		cdn.prop = 0;
		cdn.voiceHash = HashString("NO_VOICE");
		cdn.isStanding = true;
		cdn.attributeDamageToPlayer = -1;
		cdn.maxHealth = 200;
		cdn.unkBool = false;
	});

	SetupNode(tree, [resourceHash](sync::CPedSectorPosMapNode& cdn)
	{
		cdn.isStandingOn = false;
		cdn.isNM = false;
	});

	SetupPosition<sync::CSectorDataNode, sync::CPedSectorPosMapNode>(tree, posX, posY, posZ);

	SetupNode(tree, [heading](sync::CPedOrientationDataNode& node)
	{
		node.data.currentHeading = heading * 0.01745329252f;
		node.data.desiredHeading = heading * 0.01745329252f;
	});

	SetupNode(tree, [resourceHash](sync::CEntityScriptInfoDataNode& cdn)
	{
		cdn.m_scriptHash = resourceHash;
		cdn.m_timestamp = msec().count();
	});

	return tree;
}

std::shared_ptr<sync::SyncTreeBase> MakeObject(uint32_t model, float posX, float posY, float posZ, uint32_t resourceHash, bool dynamic, float heading = 0.0f)
{
	auto tree = std::make_shared<sync::CObjectSyncTree>();

	SetupNode(tree, [model, dynamic](sync::CObjectCreationDataNode& cdn)
	{
		cdn.m_model = model;
		cdn.m_hasInitPhysics = dynamic;
	});

	SetupNode(tree, [](sync::CObjectSectorPosNode& cdn)
	{
		cdn.highRes = true;
	});

	SetupPosition<sync::CSectorDataNode, sync::CObjectSectorPosNode>(tree, posX, posY, posZ);
	
	SetupNode(tree, [heading](sync::CObjectOrientationDataNode& node)
	{
		node.data.highRes = false;

		glm::quat q = glm::quat(glm::vec3(0.0f, 0.0f, heading * 0.01745329252f));
		node.data.quat.Load(q.x, q.y, q.z, q.w);
	});

	SetupNode(tree, [](sync::CGlobalFlagsDataNode& node)
	{
		node.globalFlags = 4;
		node.token = 0;
	});

	SetupNode(tree, [resourceHash](sync::CEntityScriptInfoDataNode& cdn)
	{
		cdn.m_scriptHash = resourceHash;
		cdn.m_timestamp = msec().count();
	});

	return tree;
}

template<typename TNode>
auto GetVehicleNode(sync::NetObjEntityType objectType, const std::shared_ptr<sync::SyncTreeBase>& tree)
{
	switch (objectType)
	{
		case sync::NetObjEntityType::Automobile:
			return std::static_pointer_cast<sync::CAutomobileSyncTree>(tree)->GetNode<TNode>();
		case sync::NetObjEntityType::Bike:
			return std::static_pointer_cast<sync::CBikeSyncTree>(tree)->GetNode<TNode>();
		case sync::NetObjEntityType::Boat:
			return std::static_pointer_cast<sync::CBoatSyncTree>(tree)->GetNode<TNode>();
		case sync::NetObjEntityType::Heli:
			return std::static_pointer_cast<sync::CHeliSyncTree>(tree)->GetNode<TNode>();
		case sync::NetObjEntityType::Plane:
			return std::static_pointer_cast<sync::CPlaneSyncTree>(tree)->GetNode<TNode>();
		case sync::NetObjEntityType::Submarine:
			return std::static_pointer_cast<sync::CSubmarineSyncTree>(tree)->GetNode<TNode>();
		case sync::NetObjEntityType::Trailer:
			return std::static_pointer_cast<sync::CTrailerSyncTree>(tree)->GetNode<TNode>();
		case sync::NetObjEntityType::Train:
			return std::static_pointer_cast<sync::CTrainSyncTree>(tree)->GetNode<TNode>();
		default:
			break;
	}

	return std::static_pointer_cast<sync::CAutomobileSyncTree>(tree)->GetNode<TNode>();
}

inline auto GetEntityScriptInfoNode(sync::NetObjEntityType objectType, const std::shared_ptr<sync::SyncTreeBase>& tree)
{
	switch (objectType)
	{
		case sync::NetObjEntityType::Automobile:
			return std::static_pointer_cast<sync::CAutomobileSyncTree>(tree)->GetNode<sync::CEntityScriptInfoDataNode>();
		case sync::NetObjEntityType::Bike:
			return std::static_pointer_cast<sync::CBikeSyncTree>(tree)->GetNode<sync::CEntityScriptInfoDataNode>();
		case sync::NetObjEntityType::Boat:
			return std::static_pointer_cast<sync::CBoatSyncTree>(tree)->GetNode<sync::CEntityScriptInfoDataNode>();
		case sync::NetObjEntityType::Door:
			return std::static_pointer_cast<sync::CDoorSyncTree>(tree)->GetNode<sync::CEntityScriptInfoDataNode>();
		case sync::NetObjEntityType::Heli:
			return std::static_pointer_cast<sync::CHeliSyncTree>(tree)->GetNode<sync::CEntityScriptInfoDataNode>();
		case sync::NetObjEntityType::Object:
			return std::static_pointer_cast<sync::CObjectSyncTree>(tree)->GetNode<sync::CEntityScriptInfoDataNode>();
		case sync::NetObjEntityType::Ped:
			return std::static_pointer_cast<sync::CPedSyncTree>(tree)->GetNode<sync::CEntityScriptInfoDataNode>();
		case sync::NetObjEntityType::Plane:
			return std::static_pointer_cast<sync::CPlaneSyncTree>(tree)->GetNode<sync::CEntityScriptInfoDataNode>();
		case sync::NetObjEntityType::Submarine:
			return std::static_pointer_cast<sync::CSubmarineSyncTree>(tree)->GetNode<sync::CEntityScriptInfoDataNode>();
		case sync::NetObjEntityType::Trailer:
			return std::static_pointer_cast<sync::CTrailerSyncTree>(tree)->GetNode<sync::CEntityScriptInfoDataNode>();
		case sync::NetObjEntityType::Train:
			return std::static_pointer_cast<sync::CTrainSyncTree>(tree)->GetNode<sync::CEntityScriptInfoDataNode>();
		default:
			break;
	}

	return std::static_pointer_cast<sync::CAutomobileSyncTree>(tree)->GetNode<sync::CEntityScriptInfoDataNode>();
}

void DisownEntityScript(const fx::sync::SyncEntityPtr& entity)
{
	auto tree = entity->syncTree;

	if (tree)
	{
		auto n = GetEntityScriptInfoNode(entity->type, tree);

		if (n)
		{
			auto& cdn = n->node;
			cdn.m_scriptHash = 0;
			cdn.m_timestamp = msec().count();
			UnparseTo(cdn, n);

			n->frameIndex = 12;
			n->timestamp = msec().count();
		}
	}
}

static int GetIntProp(const json& j, const std::initializer_list<const char*>& keys, int defaultVal = -1)
{
	for (const char* k : keys)
	{
		if (j.contains(k))
		{
			const auto& v = j[k];
			if (v.is_number()) return v.get<int>();
			if (v.is_string())
			{
				try { return std::stoi(v.get<std::string>()); } catch (...) {}
			}
		}
	}
	return defaultVal;
}

static float GetFloatProp(const json& j, const std::initializer_list<const char*>& keys, float defaultVal = -1.0f)
{
	for (const char* k : keys)
	{
		if (j.contains(k))
		{
			const auto& v = j[k];
			if (v.is_number()) return v.get<float>();
			if (v.is_string())
			{
				try { return std::stof(v.get<std::string>()); } catch (...) {}
			}
		}
	}
	return defaultVal;
}

static bool ParseColor(const json& j, const std::initializer_list<const char*>& keys, bool& isRGB, int& r, int& g, int& b, int& colorIndex)
{
	for (const char* k : keys)
	{
		if (!j.contains(k)) continue;
		const auto& val = j[k];
		if (val.is_number())
		{
			isRGB = false;
			colorIndex = val.get<int>();
			return true;
		}
		if (val.is_string())
		{
			std::string s = val.get<std::string>();
			if (s.size() >= 7 && s[0] == '#')
			{
				try
				{
					isRGB = true;
					r = std::stoi(s.substr(1, 2), nullptr, 16);
					g = std::stoi(s.substr(3, 2), nullptr, 16);
					b = std::stoi(s.substr(5, 2), nullptr, 16);
					return true;
				}
				catch (...) {}
			}
			try
			{
				isRGB = false;
				colorIndex = std::stoi(s);
				return true;
			}
			catch (...) {}
		}
		if (val.is_array() && val.size() >= 3)
		{
			isRGB = true;
			r = val[0].get<int>();
			g = val[1].get<int>();
			b = val[2].get<int>();
			return true;
		}
	}
	return false;
}

static void ApplyVehicleProperties(sync::NetObjEntityType typeId, const std::shared_ptr<sync::SyncTreeBase>& tree, const json& props)
{
	auto nApp = GetVehicleNode<sync::CVehicleAppearanceDataNode>(typeId, tree);
	if (nApp)
	{
		auto& data = nApp->node.data;

		// Plate
		if (props.contains("plate") && props["plate"].is_string())
		{
			std::string plateStr = props["plate"].get<std::string>();
			data.hasPlate = true;
			for (int i = 0; i < 8; i++)
			{
				data.plate[i] = (i < plateStr.size()) ? plateStr[i] : ' ';
			}
		}

		int plateIdx = GetIntProp(props, {"plateIndex", "plate_index", "numberPlateTextIndex"});
		if (plateIdx >= 0)
		{
			data.numberPlateTextIndex = plateIdx;
		}

		// Primary Color
		bool isRGB = false; int r = 0, g = 0, b = 0, cIdx = 0;
		if (ParseColor(props, {"color1", "primaryColor", "primaryColour"}, isRGB, r, g, b, cIdx))
		{
			data.isPrimaryColourRGB = isRGB;
			if (isRGB)
			{
				data.primaryRedColour = r;
				data.primaryGreenColour = g;
				data.primaryBlueColour = b;
			}
			else
			{
				data.primaryColour = cIdx;
			}
		}

		// Secondary Color
		if (ParseColor(props, {"color2", "secondaryColor", "secondaryColour"}, isRGB, r, g, b, cIdx))
		{
			data.isSecondaryColourRGB = isRGB;
			if (isRGB)
			{
				data.secondaryRedColour = r;
				data.secondaryGreenColour = g;
				data.secondaryBlueColour = b;
			}
			else
			{
				data.secondaryColour = cIdx;
			}
		}

		int pearl = GetIntProp(props, {"pearlescentColor", "pearlColor", "pearlColour"});
		if (pearl >= 0) data.pearlColour = pearl;

		int wheelCol = GetIntProp(props, {"wheelColor", "wheelColour"});
		if (wheelCol >= 0) data.wheelColour = wheelCol;

		int interiorCol = GetIntProp(props, {"interiorColor", "interiorColour"});
		if (interiorCol >= 0) data.interiorColour = interiorCol;

		int dashCol = GetIntProp(props, {"dashboardColor", "dashboardColour"});
		if (dashCol >= 0) data.dashboardColour = dashCol;

		int winTint = GetIntProp(props, {"windowTint", "window_tint", "windowTintIndex"});
		if (winTint >= 0)
		{
			data.hasWindowTint = true;
			data.windowTintIndex = winTint;
		}

		float dirt = GetFloatProp(props, {"dirtLevel", "dirt_level"});
		if (dirt >= 0.0f)
		{
			data.hasExtras = true;
			data.dirtLevel = std::clamp(int(dirt), 0, 15);
		}

		// Extras
		if (props.contains("extras"))
		{
			data.hasExtras = true;
			json extrasJson;
			if (props["extras"].is_string())
			{
				try { extrasJson = json::parse(props["extras"].get<std::string>()); } catch (...) {}
			}
			else
			{
				extrasJson = props["extras"];
			}

			if (extrasJson.is_number())
			{
				data.extras = extrasJson.get<int>();
			}
			else if (extrasJson.is_object())
			{
				for (auto& [key, val] : extrasJson.items())
				{
					try
					{
						int extraId = std::stoi(key);
						bool enabled = val.is_boolean() ? val.get<bool>() : (val.get<int>() != 0);
						if (extraId >= 0 && extraId < 16)
						{
							if (enabled) data.extras |= (1 << extraId);
							else data.extras &= ~(1 << extraId);
						}
					}
					catch (...) {}
				}
			}
		}

		// Livery
		int livery = GetIntProp(props, {"livery", "modLivery", "liveryIndex"});
		if (livery >= 0)
		{
			data.hasExtras = true;
			data.hasCustomLivery = true;
			data.liveryIndex = livery;
		}

		int roofLivery = GetIntProp(props, {"roofLivery", "roofLiveryIndex"});
		if (roofLivery >= 0)
		{
			data.hasExtras = true;
			data.hasCustomRoofLivery = true;
			data.roofLiveryIndex = roofLivery;
		}

		// Wheels
		int wheels = GetIntProp(props, {"wheels", "wheelType", "wheel_type"});
		if (wheels >= 0) data.wheelType = wheels;

		int wheelChoice = GetIntProp(props, {"wheelChoice", "modFrontWheels"});
		if (wheelChoice >= 0) data.wheelChoice = wheelChoice;

		int rearWheelChoice = GetIntProp(props, {"rearWheelChoice", "modBackWheels"});
		if (rearWheelChoice >= 0)
		{
			data.hasDifferentRearWheel = true;
			data.rearWheelChoice = rearWheelChoice;
		}

		if (props.contains("customTires") || props.contains("modCustomTiresF"))
		{
			bool ct = props.contains("customTires") ? (props["customTires"].is_boolean() ? props["customTires"].get<bool>() : (props["customTires"].get<int>() != 0))
													: (props["modCustomTiresF"].is_boolean() ? props["modCustomTiresF"].get<bool>() : (props["modCustomTiresF"].get<int>() != 0));
			data.hasCustomTires = ct;
		}

		// Mod Kit & Tuning Mods
		// In GTA V network sync, kitIndex 0 means "no mod kit installed".
		// Lua modKit 0 corresponds to network kitIndex 1.
		int modKit = GetIntProp(props, {"modKit", "kitIndex"}, -1);
		if (modKit >= 0)
		{
			data.kitIndex = (modKit == 0) ? 1 : std::clamp(modKit, 1, 3);
		}
		else
		{
			data.kitIndex = 1;
		}

		if (props.contains("mods"))
		{
			const auto& modsVal = props["mods"];
			if (modsVal.is_object())
			{
				for (auto& [key, val] : modsVal.items())
				{
					try
					{
						int slot = std::stoi(key);
						if (val.is_number())
						{
							int v = val.get<int>();
							if (slot >= 0 && slot < 13)
							{
								if (v >= 0)
								{
									data.hasMod = true;
									data.kitMods[slot] = v + 1;
								}
								else
								{
									data.kitMods[slot] = 0;
								}
							}
						}
					}
					catch (...) {}
				}
			}
			else if (modsVal.is_array())
			{
				for (size_t i = 0; i < modsVal.size() && i < 13; i++)
				{
					if (modsVal[i].is_number())
					{
						int v = modsVal[i].get<int>();
						if (v >= 0)
						{
							data.hasMod = true;
							data.kitMods[i] = v + 1;
						}
						else
						{
							data.kitMods[i] = 0;
						}
					}
				}
			}
		}

		// Named mods mapping
		static const std::unordered_map<std::string, int> namedModSlots = {
			{"modSpoilers", 0}, {"modFrontBumper", 1}, {"modRearBumper", 2},
			{"modSideSkirt", 3}, {"modExhaust", 4}, {"modFrame", 5},
			{"modGrille", 6}, {"modHood", 7}, {"modFender", 8},
			{"modRightFender", 9}, {"modRoof", 10}, {"modEngine", 11},
			{"modBrakes", 12}
		};
		for (const auto& [name, slot] : namedModSlots)
		{
			if (props.contains(name) && props[name].is_number())
			{
				int v = props[name].get<int>();
				if (v >= 0)
				{
					data.hasMod = true;
					data.kitMods[slot] = v + 1;
				}
				else
				{
					data.kitMods[slot] = 0;
				}
			}
		}

		// Neons
		if (props.contains("neonEnabled"))
		{
			json neonObj;
			if (props["neonEnabled"].is_string())
			{
				try { neonObj = json::parse(props["neonEnabled"].get<std::string>()); } catch (...) {}
			}
			else
			{
				neonObj = props["neonEnabled"];
			}

			if (neonObj.is_array() && neonObj.size() >= 4)
			{
				data.hasNeonLights = true;
				data.neonLeftOn = neonObj[0].is_boolean() ? neonObj[0].get<bool>() : (neonObj[0].get<int>() != 0);
				data.neonRightOn = neonObj[1].is_boolean() ? neonObj[1].get<bool>() : (neonObj[1].get<int>() != 0);
				data.neonFrontOn = neonObj[2].is_boolean() ? neonObj[2].get<bool>() : (neonObj[2].get<int>() != 0);
				data.neonBackOn = neonObj[3].is_boolean() ? neonObj[3].get<bool>() : (neonObj[3].get<int>() != 0);
			}
		}

		if (props.contains("neonColor"))
		{
			json ncObj;
			if (props["neonColor"].is_string())
			{
				try { ncObj = json::parse(props["neonColor"].get<std::string>()); } catch (...) {}
			}
			else
			{
				ncObj = props["neonColor"];
			}

			if (ncObj.is_array() && ncObj.size() >= 3)
			{
				data.hasNeonLights = true;
				data.neonRedColour = ncObj[0].get<int>();
				data.neonGreenColour = ncObj[1].get<int>();
				data.neonBlueColour = ncObj[2].get<int>();
			}
		}

		// Tyre smoke
		if (props.contains("tyreSmokeColor"))
		{
			json tsObj;
			if (props["tyreSmokeColor"].is_string())
			{
				try { tsObj = json::parse(props["tyreSmokeColor"].get<std::string>()); } catch (...) {}
			}
			else
			{
				tsObj = props["tyreSmokeColor"];
			}

			if (tsObj.is_array() && tsObj.size() >= 3)
			{
				data.hasTyreSmokeColours = true;
				data.tyreSmokeRedColour = tsObj[0].get<int>();
				data.tyreSmokeGreenColour = tsObj[1].get<int>();
				data.tyreSmokeBlueColour = tsObj[2].get<int>();
			}
		}

		UnparseTo(nApp->node, nApp);
		nApp->frameIndex = 12;
		nApp->timestamp = msec().count();
	}

	// Creation Data
	auto nCreation = GetVehicleNode<sync::CVehicleCreationDataNode>(typeId, tree);
	if (nCreation)
	{
		if (props.contains("bulletProofTyres") && props["bulletProofTyres"].is_boolean())
		{
			nCreation->node.m_tyresDontBurst = props["bulletProofTyres"].get<bool>();
			UnparseTo(nCreation->node, nCreation);
			nCreation->frameIndex = 12;
			nCreation->timestamp = msec().count();
		}
	}
}

static void ApplyVehicleEntityProperties(const fx::sync::SyncEntityPtr& entity, const json& props, float x, float y, float z)
{
	if (!entity) return;

	// Plate authoritative
	if (props.contains("plate") && props["plate"].is_string())
	{
		std::string plateStr = props["plate"].get<std::string>();
		entity->plateAuthoritative = true;
		strncpy(entity->authoritativePlate, plateStr.c_str(), sizeof(entity->authoritativePlate) - 1);
		entity->authoritativePlate[sizeof(entity->authoritativePlate) - 1] = '\0';
	}

	// Doors / Lock status
	int lockStatus = GetIntProp(props, {"lockStatus", "door_status", "doorsLocked"});
	if (lockStatus > 0)
	{
		entity->doorsServerAuthoritative = true;
		entity->authoritativeLockStatus = lockStatus;
	}

	// Engine health authoritative
	float engHealth = GetFloatProp(props, {"engineHealth", "health_engine", "healthEngine"});
	if (engHealth >= 0.0f)
	{
		entity->engineHealthAuthoritative = true;
		entity->authoritativeEngineHealth = engHealth;
	}

	// Orphan mode (defaults to KeepEntity for server setters unless specified)
	int orphan = GetIntProp(props, {"orphanMode", "orphan_mode"}, (int)sync::EntityOrphanMode::KeepEntity);
	if (orphan >= 0)
	{
		entity->orphanMode = (sync::EntityOrphanMode)orphan;
	}

	// Frozen
	if (props.contains("frozen") && (props["frozen"].is_boolean() ? props["frozen"].get<bool>() : (props["frozen"].get<int>() != 0)))
	{
		entity->isFrozen = true;
		entity->frozenCoords[0] = x;
		entity->frozenCoords[1] = y;
		entity->frozenCoords[2] = z;
	}
}

static void ApplyPedProperties(const std::shared_ptr<sync::SyncTreeBase>& tree, const json& props)
{
	auto nCreation = std::static_pointer_cast<sync::CPedSyncTree>(tree)->GetNode<sync::CPedCreationDataNode>();
	if (nCreation)
	{
		int maxHp = GetIntProp(props, {"maxHealth", "max_health"});
		if (maxHp > 0)
		{
			nCreation->node.maxHealth = maxHp;
			UnparseTo(nCreation->node, nCreation);
			nCreation->frameIndex = 12;
			nCreation->timestamp = msec().count();
		}
	}
}

static void ApplyPedEntityProperties(const fx::sync::SyncEntityPtr& entity, const json& props, float x, float y, float z)
{
	if (!entity) return;

	int orphan = GetIntProp(props, {"orphanMode", "orphan_mode"}, (int)sync::EntityOrphanMode::KeepEntity);
	if (orphan >= 0)
	{
		entity->orphanMode = (sync::EntityOrphanMode)orphan;
	}

	if (props.contains("frozen") && (props["frozen"].is_boolean() ? props["frozen"].get<bool>() : (props["frozen"].get<int>() != 0)))
	{
		entity->isFrozen = true;
		entity->frozenCoords[0] = x;
		entity->frozenCoords[1] = y;
		entity->frozenCoords[2] = z;
	}

	if (props.contains("invincible") && (props["invincible"].is_boolean() ? props["invincible"].get<bool>() : (props["invincible"].get<int>() != 0)))
	{
		entity->isInvincible = true;
	}
}

static void ApplyObjectEntityProperties(const fx::sync::SyncEntityPtr& entity, const json& props, float x, float y, float z)
{
	if (!entity) return;

	int orphan = GetIntProp(props, {"orphanMode", "orphan_mode"}, (int)sync::EntityOrphanMode::KeepEntity);
	if (orphan >= 0)
	{
		entity->orphanMode = (sync::EntityOrphanMode)orphan;
	}

	if (props.contains("frozen") && (props["frozen"].is_boolean() ? props["frozen"].get<bool>() : (props["frozen"].get<int>() != 0)))
	{
		entity->isFrozen = true;
		entity->frozenCoords[0] = x;
		entity->frozenCoords[1] = y;
		entity->frozenCoords[2] = z;
	}
}

static InitFunction initFunction([]()
{
	fx::ServerInstanceBase::OnServerCreate.Connect([](fx::ServerInstanceBase* ref)
	{
		if (!IsStateGame())
		{
			return;
		}

		fx::ScriptEngine::RegisterNativeHandler("CREATE_AUTOMOBILE", [=](fx::ScriptContext& ctx) 
		{
			uint32_t resourceHash = 0;

			fx::OMPtr<IScriptRuntime> runtime;

			if (FX_SUCCEEDED(fx::GetCurrentScriptRuntime(&runtime)))
			{
				fx::Resource* resource = reinterpret_cast<fx::Resource*>(runtime->GetParentObject());

				if (resource)
				{
					resourceHash = HashString(resource->GetName().c_str());
				}
			}

			auto tree = MakeAutomobile(ctx.GetArgument<uint32_t>(0), ctx.GetArgument<float>(1), ctx.GetArgument<float>(2), ctx.GetArgument<float>(3), resourceHash, ctx.GetArgument<float>(4));

			auto sgs = ref->GetComponent<fx::ServerGameState>();
			auto entity = sgs->CreateEntityFromTree(sync::NetObjEntityType::Automobile, tree);

			uint32_t guid = 0;

			if (entity)
			{
				guid = sgs->MakeScriptHandle(entity);
			}

			ctx.SetResult(guid);
		});

		fx::ScriptEngine::RegisterNativeHandler("CREATE_VEHICLE_SERVER_SETTER", [ref](fx::ScriptContext& ctx)
		{
			uint32_t modelHash = ctx.GetArgument<uint32_t>(0);
			std::string_view type = ctx.CheckArgument<const char*>(1);
			float x = ctx.GetArgument<float>(2);
			float y = ctx.GetArgument<float>(3);
			float z = ctx.GetArgument<float>(4);
			float heading = ctx.GetArgument<float>(5);

			uint32_t resourceHash = 0;
			fx::OMPtr<IScriptRuntime> runtime;

			if (FX_SUCCEEDED(fx::GetCurrentScriptRuntime(&runtime)))
			{
				fx::Resource* resource = reinterpret_cast<fx::Resource*>(runtime->GetParentObject());

				if (resource)
				{
					resourceHash = HashString(resource->GetName().c_str());
				}
			}

			std::shared_ptr<fx::sync::SyncTreeBase> tree;
			auto typeId = (fx::sync::NetObjEntityType)-1;

			if (type == "automobile")
			{
				typeId = sync::NetObjEntityType::Automobile;
			}
			else if (type == "bike")
			{
				typeId = sync::NetObjEntityType::Bike;
			}
			else if (type == "boat")
			{
				typeId = sync::NetObjEntityType::Boat;
			}
			else if (type == "heli")
			{
				typeId = sync::NetObjEntityType::Heli;
			}
			else if (type == "plane")
			{
				typeId = sync::NetObjEntityType::Plane;
			}
			else if (type == "submarine")
			{
				typeId = sync::NetObjEntityType::Submarine;
			}
			else if (type == "trailer")
			{
				typeId = sync::NetObjEntityType::Trailer;
			}
			else if (type == "train")
			{
				typeId = sync::NetObjEntityType::Train;
			}
			else
			{
				throw std::runtime_error(va("CREATE_VEHICLE_SERVER_SETTER: Invalid entity type %s", type));
			}

			switch (typeId)
			{
				case fx::sync::NetObjEntityType::Automobile:
					tree = MakeVehicle<fx::sync::CAutomobileSyncTree>(modelHash, x, y, z, resourceHash, heading);
					break;
				case fx::sync::NetObjEntityType::Bike:
					tree = MakeVehicle<fx::sync::CBikeSyncTree>(modelHash, x, y, z, resourceHash, heading);
					break;
				case fx::sync::NetObjEntityType::Boat:
					tree = MakeVehicle<fx::sync::CBoatSyncTree>(modelHash, x, y, z, resourceHash, heading);
					break;
				case fx::sync::NetObjEntityType::Heli:
					tree = MakeVehicle<fx::sync::CHeliSyncTree>(modelHash, x, y, z, resourceHash, heading);
					break;
				case fx::sync::NetObjEntityType::Plane:
					tree = MakeVehicle<fx::sync::CPlaneSyncTree>(modelHash, x, y, z, resourceHash, heading);
					break;
				case fx::sync::NetObjEntityType::Submarine:
					tree = MakeVehicle<fx::sync::CSubmarineSyncTree>(modelHash, x, y, z, resourceHash, heading);
					break;
				case fx::sync::NetObjEntityType::Trailer:
					tree = MakeVehicle<fx::sync::CTrailerSyncTree>(modelHash, x, y, z, resourceHash, heading);
					break;
				case fx::sync::NetObjEntityType::Train:
					tree = MakeVehicle<fx::sync::CTrainSyncTree>(modelHash, x, y, z, resourceHash, heading);
					break;
			}

			const char* jsonStr = nullptr;
			if (ctx.GetArgumentCount() > 6)
			{
				jsonStr = ctx.GetArgument<const char*>(6);
			}

			json props;
			if (jsonStr && jsonStr[0] != '\0')
			{
				try
				{
					props = json::parse(jsonStr);
				}
				catch (...)
				{
				}
			}

			if (!props.is_null() && !props.empty())
			{
				ApplyVehicleProperties(typeId, tree, props);
			}

			auto sgs = ref->GetComponent<fx::ServerGameState>();
			auto entity = sgs->CreateEntityFromTree(typeId, tree);

			if (entity && !props.is_null() && !props.empty())
			{
				ApplyVehicleEntityProperties(entity, props, x, y, z);
			}

			uint32_t guid = 0;

			if (entity)
			{
				guid = sgs->MakeScriptHandle(entity);
			}

			ctx.SetResult(guid);
		});

		fx::ScriptEngine::RegisterNativeHandler("CREATE_PED", [=](fx::ScriptContext& ctx)
		{
			uint32_t resourceHash = 0;

			fx::OMPtr<IScriptRuntime> runtime;

			if (FX_SUCCEEDED(fx::GetCurrentScriptRuntime(&runtime)))
			{
				fx::Resource* resource = reinterpret_cast<fx::Resource*>(runtime->GetParentObject());

				if (resource)
				{
					resourceHash = HashString(resource->GetName().c_str());
				}
			}

			auto tree = MakePed(ctx.GetArgument<uint32_t>(1), ctx.GetArgument<float>(2), ctx.GetArgument<float>(3), ctx.GetArgument<float>(4), resourceHash, ctx.GetArgument<float>(5));

			auto sgs = ref->GetComponent<fx::ServerGameState>();
			auto entity = sgs->CreateEntityFromTree(sync::NetObjEntityType::Ped, tree);

			uint32_t guid = 0;

			if (entity)
			{
				guid = sgs->MakeScriptHandle(entity);
			}

			ctx.SetResult(guid);
		});

		fx::ScriptEngine::RegisterNativeHandler("CREATE_OBJECT_NO_OFFSET", [=](fx::ScriptContext& ctx)
		{
			uint32_t resourceHash = 0;

			fx::OMPtr<IScriptRuntime> runtime;

			if (FX_SUCCEEDED(fx::GetCurrentScriptRuntime(&runtime)))
			{
				fx::Resource* resource = reinterpret_cast<fx::Resource*>(runtime->GetParentObject());

				if (resource)
				{
					resourceHash = HashString(resource->GetName().c_str());
				}
			}

			auto tree = MakeObject(ctx.GetArgument<uint32_t>(0), ctx.GetArgument<float>(1), ctx.GetArgument<float>(2), ctx.GetArgument<float>(3), resourceHash, ctx.GetArgument<bool>(6));

			auto sgs = ref->GetComponent<fx::ServerGameState>();
			auto entity = sgs->CreateEntityFromTree(sync::NetObjEntityType::Object, tree);

			uint32_t guid = 0;

			if (entity)
			{
				guid = sgs->MakeScriptHandle(entity);
			}

			ctx.SetResult(guid);
		});

		fx::ScriptEngine::RegisterNativeHandler("CREATE_PED_SERVER_SETTER", [=](fx::ScriptContext& ctx)
		{
			uint32_t resourceHash = 0;

			fx::OMPtr<IScriptRuntime> runtime;

			if (FX_SUCCEEDED(fx::GetCurrentScriptRuntime(&runtime)))
			{
				fx::Resource* resource = reinterpret_cast<fx::Resource*>(runtime->GetParentObject());

				if (resource)
				{
					resourceHash = HashString(resource->GetName().c_str());
				}
			}

			uint32_t model = 0;
			float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
			const char* jsonStr = nullptr;

			if (ctx.GetArgumentCount() >= 6 && ctx.GetArgument<uint32_t>(1) != 0 && ctx.GetArgument<float>(2) != 0.0f)
			{
				model = ctx.GetArgument<uint32_t>(1);
				x = ctx.GetArgument<float>(2);
				y = ctx.GetArgument<float>(3);
				z = ctx.GetArgument<float>(4);
				heading = ctx.GetArgument<float>(5);
				if (ctx.GetArgumentCount() > 6)
				{
					jsonStr = ctx.GetArgument<const char*>(6);
				}
			}
			else
			{
				model = ctx.GetArgument<uint32_t>(0);
				x = ctx.GetArgument<float>(1);
				y = ctx.GetArgument<float>(2);
				z = ctx.GetArgument<float>(3);
				heading = (ctx.GetArgumentCount() > 4) ? ctx.GetArgument<float>(4) : 0.0f;
				if (ctx.GetArgumentCount() > 5)
				{
					jsonStr = ctx.GetArgument<const char*>(5);
				}
			}

			auto tree = MakePed(model, x, y, z, resourceHash, heading);

			json props;
			if (jsonStr && jsonStr[0] != '\0')
			{
				try { props = json::parse(jsonStr); } catch (...) {}
			}

			if (!props.is_null() && !props.empty())
			{
				ApplyPedProperties(tree, props);
			}

			auto sgs = ref->GetComponent<fx::ServerGameState>();
			auto entity = sgs->CreateEntityFromTree(sync::NetObjEntityType::Ped, tree);

			if (entity && !props.is_null() && !props.empty())
			{
				ApplyPedEntityProperties(entity, props, x, y, z);
			}

			uint32_t guid = 0;

			if (entity)
			{
				guid = sgs->MakeScriptHandle(entity);
			}

			ctx.SetResult(guid);
		});

		fx::ScriptEngine::RegisterNativeHandler("CREATE_OBJECT_SERVER_SETTER", [=](fx::ScriptContext& ctx)
		{
			uint32_t resourceHash = 0;

			fx::OMPtr<IScriptRuntime> runtime;

			if (FX_SUCCEEDED(fx::GetCurrentScriptRuntime(&runtime)))
			{
				fx::Resource* resource = reinterpret_cast<fx::Resource*>(runtime->GetParentObject());

				if (resource)
				{
					resourceHash = HashString(resource->GetName().c_str());
				}
			}

			uint32_t model = ctx.GetArgument<uint32_t>(0);
			float x = ctx.GetArgument<float>(1);
			float y = ctx.GetArgument<float>(2);
			float z = ctx.GetArgument<float>(3);
			float heading = (ctx.GetArgumentCount() > 4) ? ctx.GetArgument<float>(4) : 0.0f;
			bool dynamic = (ctx.GetArgumentCount() > 5) ? ctx.GetArgument<bool>(5) : false;
			const char* jsonStr = (ctx.GetArgumentCount() > 6) ? ctx.GetArgument<const char*>(6) : nullptr;

			auto tree = MakeObject(model, x, y, z, resourceHash, dynamic, heading);

			json props;
			if (jsonStr && jsonStr[0] != '\0')
			{
				try { props = json::parse(jsonStr); } catch (...) {}
			}

			auto sgs = ref->GetComponent<fx::ServerGameState>();
			auto entity = sgs->CreateEntityFromTree(sync::NetObjEntityType::Object, tree);

			if (entity && !props.is_null() && !props.empty())
			{
				ApplyObjectEntityProperties(entity, props, x, y, z);
			}

			uint32_t guid = 0;

			if (entity)
			{
				guid = sgs->MakeScriptHandle(entity);
			}

			ctx.SetResult(guid);
		});
	});
});
}
#else
namespace fx
{
void DisownEntityScript(const fx::sync::SyncEntityPtr& entity)
{
}
}
#endif

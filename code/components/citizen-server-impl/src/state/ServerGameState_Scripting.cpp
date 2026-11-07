#include <StdInc.h>

#include <ServerInstanceBase.h>
#include <ServerInstanceBaseRef.h>
#include <state/ServerGameState.h>

#include <ResourceEventComponent.h>
#include <ResourceManager.h>
#include <ScriptEngine.h>
#include <ScriptDeprecations.h>

#include <ScriptSerialization.h>
#include <MakeClientFunction.h>
#include <MakePlayerEntityFunction.h>

namespace fx
{
void DisownEntityScript(const fx::sync::SyncEntityPtr& entity);
}

static const char* SerializeIntListToJson(const std::vector<int>& list)
{
	static thread_local std::string s_jsonBuffer;
	s_jsonBuffer.clear();
	s_jsonBuffer.reserve(list.size() * 8 + 2);
	s_jsonBuffer += '[';
	for (size_t i = 0; i < list.size(); ++i)
	{
		if (i > 0)
		{
			s_jsonBuffer += ',';
		}
		s_jsonBuffer += std::to_string(list[i]);
	}
	s_jsonBuffer += ']';
	return s_jsonBuffer.c_str();
}

static void Init()
{


	// If the entity is in its deleting/finalizing state we should not allow access to them
	// This should only be used for natives that are not expected to work when an entity is actively being deleted
	// i.e. setters, getting entity by network id / does exist, or any pool getter natives
	static auto IsEntityValid = [](const fx::sync::SyncEntityPtr& entity) {
		return entity && !entity->deleting && !entity->finalizing;
	};

	auto makeEntityFunction = [](auto fn, uintptr_t defaultValue = 0)
	{
		return [=](fx::ScriptContext& context)
		{
			// get the current resource manager
			auto resourceManager = fx::ResourceManager::GetCurrent();

			// get the owning server instance
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

			// get the server's game state
			auto gameState = instance->GetComponent<fx::ServerGameState>();

			// parse the client ID
			auto id = context.GetArgument<uint32_t>(0);

			if (!id)
			{
				context.SetResult(defaultValue);
				return;
			}

			auto entity = gameState->GetEntity(id);

			if (!entity)
			{
				throw std::runtime_error(va("Tried to access invalid entity: %d", id));

				context.SetResult(defaultValue);
				return;
			}

			context.SetResult(fn(context, entity));
		};
	};



	struct scrVector
	{
		float x;
		int pad;
		float y;
		int pad2;
		float z;
		int pad3;
	};

	enum EntityType
	{
		NoEntity = 0,
		Ped = 1,
		Vehicle = 2,
		Object = 3,
	};

	static auto GetEntityType = [](const fx::sync::SyncEntityPtr& entity)
	{
		switch (entity->type)
		{
			case fx::sync::NetObjEntityType::Automobile:
			case fx::sync::NetObjEntityType::Bike:
			case fx::sync::NetObjEntityType::Boat:
			case fx::sync::NetObjEntityType::Heli:
			case fx::sync::NetObjEntityType::Plane:
			case fx::sync::NetObjEntityType::Submarine:
			case fx::sync::NetObjEntityType::Trailer:
			case fx::sync::NetObjEntityType::Train:
#ifdef STATE_RDR3
			case fx::sync::NetObjEntityType::DraftVeh:
#endif
				return EntityType::Vehicle;
			case fx::sync::NetObjEntityType::Ped:
			case fx::sync::NetObjEntityType::Player:
#ifdef STATE_RDR3
			case fx::sync::NetObjEntityType::Animal:
			case fx::sync::NetObjEntityType::Horse:
#endif
				return EntityType::Ped;
			case fx::sync::NetObjEntityType::Object:
			case fx::sync::NetObjEntityType::Door:
			case fx::sync::NetObjEntityType::Pickup:
#ifdef STATE_RDR3
			case fx::sync::NetObjEntityType::WorldProjectile:
#endif
				return EntityType::Object;
			default:
				return EntityType::NoEntity;
		}
	};


	fx::ScriptEngine::RegisterNativeHandler("DOES_ENTITY_EXIST", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		// parse the client ID
		auto id = context.GetArgument<uint32_t>(0);

		if (!id)
		{
			context.SetResult(false);
			return;
		}

		auto entity = gameState->GetEntity(id);

		if (!IsEntityValid(entity))
		{
			context.SetResult(false);
			return;
		}

		context.SetResult(true);
	});

#if _DEBUG 
	fx::ScriptEngine::RegisterNativeHandler("IS_ENTITY_RELEVANT", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		// parse the client ID
		auto id = context.GetArgument<uint32_t>(0);

		if (!id)
		{
			context.SetResult(false);
			return;
		}

		auto entity = gameState->GetEntity(id);

		if (!IsEntityValid(entity))
		{
			context.SetResult(false);
			return;
		}

		std::lock_guard l(entity->guidMutex);

		context.SetResult(entity->relevantTo.any());
	});
#endif

	fx::ScriptEngine::RegisterNativeHandler("NETWORK_GET_ENTITY_FROM_NETWORK_ID", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		// parse the client ID
		auto id = context.GetArgument<uint32_t>(0);

		if (!id)
		{
			context.SetResult(0);
			return;
		}

		auto entity = gameState->GetEntity(0, id);

		if (!IsEntityValid(entity))
		{
			context.SetResult(0);
			return;
		}

		context.SetResult(gameState->MakeScriptHandle(entity));
	});

	fx::ScriptEngine::RegisterNativeHandler("NETWORK_GET_ENTITY_OWNER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		int retval = -1;
		auto entry = entity->GetClient();

		if (entry)
		{
			retval = entry->GetNetId();
		}

		return retval;
	}));

	fx::ScriptEngine::RegisterNativeHandler("NETWORK_GET_FIRST_ENTITY_OWNER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
    {
        int retval = -1;
        auto firstOwner = entity->GetFirstOwner();

        if (firstOwner && !entity->firstOwnerDropped)
        {
            retval = firstOwner->GetNetId();
        }

        return retval;
    }));

	fx::ScriptEngine::RegisterNativeHandler("SET_ENTITY_ORPHAN_MODE", [](fx::ScriptContext& context)
    {
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		// parse the client ID
		auto id = context.GetArgument<uint32_t>(0);

		if (!id)
		{
			return;
		}

		auto entity = gameState->GetEntity(id);

		if (!IsEntityValid(entity))
		{
			throw std::runtime_error(va("Tried to access invalid entity: %d", id));
		}

		int rawOrphanMode = context.GetArgument<int>(1);

		if (rawOrphanMode < 0 || rawOrphanMode > fx::sync::KeepEntity)
		{
			throw std::runtime_error(va("Tried to set entities (%d) orphan mode to an invalid orphan mode: %d", id, rawOrphanMode));
		}


		fx::sync::EntityOrphanMode entityOrphanMode = static_cast<fx::sync::EntityOrphanMode>(rawOrphanMode);

#ifdef STATE_FIVE
		if (entity->type == fx::sync::NetObjEntityType::Train)
		{
			// recursively apply orphan mode to all of the trains children/parents
			gameState->IterateTrainLink(entity, [entityOrphanMode](fx::sync::SyncEntityPtr& train) {
				train->orphanMode = entityOrphanMode;

				return true;
			});
		}
		else
#endif
		{
			entity->orphanMode = entityOrphanMode;
		}

		// if they set the orphan mode to `DeleteOnOwnerDisconnect` and the entity already doesn't have an owner then treat this as a `DELETE_ENTITY` call
		if (entity->orphanMode == fx::sync::DeleteOnOwnerDisconnect && entity->firstOwnerDropped)
		{
			gameState->DeleteEntity(entity);
		}
    });

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_ORPHAN_MODE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return entity->orphanMode;
	}));

#ifdef STATE_FIVE
	fx::ScriptEngine::RegisterNativeHandler("SET_ENTITY_REMOTE_SYNCED_SCENES_ALLOWED", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		entity->allowRemoteSyncedScenes = context.GetArgument<bool>(1);

		return true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_REMOTE_SYNCED_SCENES_ALLOWED", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return entity->allowRemoteSyncedScenes;
	}));
#endif

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_COORDS", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		float position[3];
		entity->syncTree->GetPosition(position);

		scrVector resultVec = { 0 };
		resultVec.x = position[0];
		resultVec.y = position[1];
		resultVec.z = position[2];

		return resultVec;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_VELOCITY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		scrVector resultVec = { 0 };

		auto v = entity->syncTree->GetVelocity();

		if (v)
		{
			resultVec.x = v->velX;
			resultVec.y = v->velY;
			resultVec.z = v->velZ;
		}

		return resultVec;
	}));

	static const float pi = 3.14159265358979323846f;

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_ROTATION_VELOCITY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto av = entity->syncTree->GetAngVelocity();

		scrVector resultVec = { 0 };

		if (av)
		{
			resultVec.x = av->angVelX;
			resultVec.y = av->angVelY;
			resultVec.z = av->angVelZ;
		}

		return resultVec;
	}));

	static auto GetEntityRotation = [](const fx::sync::SyncEntityPtr& entity, scrVector& resultVec)
	{
		if (GetEntityType(entity) == EntityType::Ped)
		{
			resultVec.x = 0.0f;
			resultVec.y = 0.0f;

			auto pn = entity->syncTree->GetPedOrientation();

			if (pn)
			{
				resultVec.z = pn->currentHeading * 180.0 / pi;
			}
		}
		else
		{
#ifdef STATE_FIVE
			auto en = entity->syncTree->GetEntityOrientation();
			auto on = entity->syncTree->GetObjectOrientation();

			if (en || on)
			{
				bool highRes = false;
				fx::sync::compressed_quaternion<11> quat;
				float rotX, rotY, rotZ;

				if (en)
				{
					quat = en->quat;
				}
				else if (on)
				{
					highRes = on->highRes;
					quat = on->quat;
					rotX = on->rotX;
					rotY = on->rotY;
					rotZ = on->rotZ;
				}

				if (highRes)
				{
					resultVec.x = rotX * 180.0 / pi;
					resultVec.y = rotY * 180.0 / pi;
					resultVec.z = rotZ * 180.0 / pi;
				}
				else
				{
					float qx, qy, qz, qw;
					quat.Save(qx, qy, qz, qw);

					auto m4 = glm::toMat4(glm::quat{ qw, qx, qy, qz });

					// common GTA rotation (2) is ZXY
					glm::extractEulerAngleZXY(m4, resultVec.z, resultVec.x, resultVec.y);

					resultVec.x = glm::degrees(resultVec.x);
					resultVec.y = glm::degrees(resultVec.y);
					resultVec.z = glm::degrees(resultVec.z);
				}
			}
#elif STATE_RDR3

			auto en = entity->syncTree->GetEntityOrientation();
			if (en)
			{
				resultVec.x = en->rotX * 180.0f / pi;
				resultVec.y = en->rotY * 180.0f / pi;
				resultVec.z = en->rotZ * 180.0f / pi;
			}
#endif
		}
	};

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_ROTATION", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		scrVector resultVec = { 0 };
		GetEntityRotation(entity, resultVec);
		return resultVec;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_HEADING", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		scrVector resultVec = { 0 };
		GetEntityRotation(entity, resultVec);
		return (resultVec.z < 0) ? 360.0f + resultVec.z : resultVec.z;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_POPULATION_TYPE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		fx::sync::ePopType popType = fx::sync::POPTYPE_UNKNOWN;
		entity->syncTree->GetPopulationType(&popType);

		return popType;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_MODEL", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		uint32_t model = 0;
		entity->syncTree->GetModelHash(&model);

		return model;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_SCRIPT", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		uint32_t script = 0;
		if (entity->syncTree->GetScriptHash(&script))
		{
			static std::string scriptName;
			scriptName.clear();

			auto resourceManager = fx::ResourceManager::GetCurrent();
			resourceManager->ForAllResources([script](const fwRefContainer<fx::Resource>& resource)
			{
				if (scriptName.empty())
				{
					std::string subName = resource->GetName();

					if (subName.length() > 63)
					{
						subName = subName.substr(0, 63);
					}

					if (HashString(subName.c_str()) == script)
					{
						scriptName = resource->GetName();
					}
				}
			});

			if (!scriptName.empty())
			{
				return scriptName.c_str();
			}
		}

		return (const char*)nullptr;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_TYPE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity) -> const char*
	{
		switch (entity->type)
		{
			case fx::sync::NetObjEntityType::Automobile:
				return "automobile";
			case fx::sync::NetObjEntityType::Bike:
				return "bike";
			case fx::sync::NetObjEntityType::Boat:
				return "boat";
			case fx::sync::NetObjEntityType::Heli:
				return "heli";
			case fx::sync::NetObjEntityType::Plane:
				return "plane";
			case fx::sync::NetObjEntityType::Submarine:
				return "submarine";
			case fx::sync::NetObjEntityType::Trailer:
				return "trailer";
			case fx::sync::NetObjEntityType::Train:
				return "train";
		}

		return nullptr;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_TYPE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return (int)GetEntityType(entity);
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_NET_TYPE_FROM_ENTITY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return entity->type;
	}, -1));

	fx::ScriptEngine::RegisterNativeHandler("SET_ROUTING_BUCKET_POPULATION_ENABLED", [](fx::ScriptContext& context)
	{
		int bucket = context.GetArgument<int>(0);
		bool enabled = context.GetArgument<bool>(1);

		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		gameState->SetPopulationDisabled(bucket, !enabled);
	});

	fx::ScriptEngine::RegisterNativeHandler("SET_ROUTING_BUCKET_ENTITY_LOCKDOWN_MODE", [](fx::ScriptContext& context)
	{
		int bucket = context.GetArgument<int>(0);
		std::string_view sv = context.CheckArgument<const char*>(1);

		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		if (sv == "strict")
		{
			gameState->SetEntityLockdownMode(bucket, fx::EntityLockdownMode::Strict);
		}
		else if (sv == "no_dummy")
		{
			gameState->SetEntityLockdownMode(bucket, fx::EntityLockdownMode::Dummy);
		}
		else if (sv == "relaxed")
		{
			gameState->SetEntityLockdownMode(bucket, fx::EntityLockdownMode::Relaxed);
		}
		else if (sv == "inactive")
		{
			gameState->SetEntityLockdownMode(bucket, fx::EntityLockdownMode::Inactive);
		}
	});

	fx::ScriptEngine::RegisterNativeHandler("SET_SYNC_ENTITY_LOCKDOWN_MODE", [](fx::ScriptContext& context)
	{
		std::string_view sv = context.CheckArgument<const char*>(0);

		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		if (sv == "strict")
		{
			gameState->SetEntityLockdownMode(fx::EntityLockdownMode::Strict);
		}
		else if (sv == "no_dummy")
		{
			gameState->SetEntityLockdownMode(fx::EntityLockdownMode::Dummy);
		}
		else if (sv == "relaxed")
		{
			gameState->SetEntityLockdownMode(fx::EntityLockdownMode::Relaxed);
		}
		else if (sv == "inactive")
		{
			gameState->SetEntityLockdownMode(fx::EntityLockdownMode::Inactive);
		}
	});

	fx::ScriptEngine::RegisterNativeHandler("NETWORK_GET_NETWORK_ID_FROM_ENTITY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return entity->handle & 0xFFFF;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HASH_KEY", [](fx::ScriptContext& context)
	{
		context.SetResult(HashString(context.CheckArgument<const char*>(0)));
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_RADIO_STATION_INDEX", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleGameState();

		return vn ? vn->radioStation : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_LIGHTS_STATE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 2)
		{
			auto vn = entity->syncTree->GetVehicleGameState();

			*context.GetArgument<int*>(1) = vn ? vn->lightsOn : false;
			*context.GetArgument<int*>(2) = vn ? vn->highbeamsOn : false;
		}

		return 1;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_IS_VEHICLE_ENGINE_RUNNING", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleGameState();

		return vn ? vn->isEngineOn : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_VEHICLE_ENGINE_STARTING", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleGameState();

		return vn ? vn->isEngineStarting : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_HANDBRAKE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleGameState();

		return vn ? vn->handbrake : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_HEADLIGHTS_COLOUR", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		int headlightsColour = 0;

		auto vn = entity->syncTree->GetVehicleGameState();

		if (vn)
		{
			if (vn->defaultHeadlights)
			{
				headlightsColour = vn->headlightsColour;
			}
		}

		return headlightsColour;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_VEHICLE_SIREN_ON", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleGameState();

		return vn ? vn->sirenOn : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_DOOR_STATUS", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleGameState();

		if (!vn)
		{
			return 0;
		}

		int doorStatus = 0;
		int doorsOpen = vn->doorsOpen;

		if (context.GetArgumentCount() > 1 && doorsOpen)
		{
			const int index = context.GetArgument<int>(1);
			if (index < 0 || index > 6)
			{
				return doorStatus;
			}

			doorStatus = vn->doorPositions[index];
		}

		return doorStatus;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_DOOR_LOCK_STATUS", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleGameState();

		return vn ? vn->lockStatus : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_DOORS_LOCKED_FOR_PLAYER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleGameState();

		if (!vn)
		{
			return false;
		}

		bool lockedForPlayer = false;
		int hasLock = vn->hasLock;

		if (hasLock)
		{
			int lockedPlayers = vn->lockedPlayers;

			if (lockedPlayers == -1) // ALL
			{
				lockedForPlayer = true;
			}
		}

		return lockedForPlayer;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_ENGINE_HEALTH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleHealth();

		return vn ? float(vn->engineHealth) : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_PETROL_TANK_HEALTH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleHealth();

		return vn ? float(vn->petrolTankHealth) : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_VEHICLE_TYRE_BURST", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleHealth();

		if (!vn)
		{
			return false;
		}

		bool tyreBurst = false;
		bool wheelsFine = vn->tyresFine;

		if (!wheelsFine)
		{
			const int tyreID = context.GetArgument<int>(1);
			if (tyreID < 0 || tyreID > 15)
			{
				return tyreBurst;
			}

			bool completely = context.GetArgument<bool>(2);

			int tyreStatus = vn->tyreStatus[tyreID];

			if (completely && tyreStatus == 2 || !completely && tyreStatus == 1)
			{
				tyreBurst = true;
			}
		}

		return tyreBurst;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_BODY_HEALTH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleHealth();

		return vn ? float(vn->bodyHealth) : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_MAX_HEALTH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPedHealth();

		return pn ? pn->maxHealth : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_ARMOUR", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPedHealth();

		return pn ? pn->armour : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_CAUSE_OF_DEATH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPedHealth();

		return pn ? pn->causeOfDeath : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_DESIRED_HEADING", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (GetEntityType(entity) == EntityType::Ped)
		{
			auto pn = entity->syncTree->GetPedOrientation();

			if (pn)
			{
				float heading = pn->desiredHeading * 180.0 / pi;
				return (heading < 0) ? 360.0f + heading : heading;
			}
		}

		return 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_MAX_HEALTH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (GetEntityType(entity) == EntityType::Ped)
		{
			auto pn = entity->syncTree->GetPedHealth();
			return pn ? pn->maxHealth : 0;
		}

		return 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_HEALTH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		EntityType entType = GetEntityType(entity);

		if (entType == EntityType::Ped)
		{
			auto pn = entity->syncTree->GetPedHealth();
			return pn ? pn->health : 0;
		}
		else if (entType == EntityType::Vehicle)
		{
			auto pn = entity->syncTree->GetVehicleHealth();
			return pn ? pn->health : 0;
		}

		return 0;
	}));
	
	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_NEON_COLOUR", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		const auto vn = entity->syncTree->GetVehicleAppearance();
		
		if (context.GetArgumentCount() > 2)
		{
			if (!vn->hasNeonLights)
			{
				*context.GetArgument<int*>(1) = -1;
				*context.GetArgument<int*>(2) = -1;
				*context.GetArgument<int*>(3) = -1;
				return 1;
			}
			*context.GetArgument<int*>(1) = vn ? vn->neonRedColour : 0;
			*context.GetArgument<int*>(2) = vn ? vn->neonGreenColour : 0;
			*context.GetArgument<int*>(3) = vn ? vn->neonBlueColour : 0;
		}

		return 1;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_NEON_ENABLED", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		const auto vn = entity->syncTree->GetVehicleAppearance();

		if (!vn)
		{
			return false;
		}
		
		const int neonIndex = context.GetArgument<int>(0);
		
		switch (neonIndex)
		{
			case 0: // Left neon light
				return vn->neonLeftOn;

			case 1: // Right neon light
				return vn->neonRightOn;

			case 2: // Front neon light
				return vn->neonFrontOn;

			case 3: // Back neon light
				return vn->neonBackOn;

			default:
				return false;
		}
	}));
	
	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_COLOURS", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 2)
		{
			auto vn = entity->syncTree->GetVehicleAppearance();

			*context.GetArgument<int*>(1) = vn ? vn->primaryColour : 0;
			*context.GetArgument<int*>(2) = vn ? vn->secondaryColour : 0;
		}

		return 1;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_HORN_TYPE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleAppearance();

		return vn ? vn->hornTypeHash : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_EXTRA_COLOURS", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 2)
		{
			auto vn = entity->syncTree->GetVehicleAppearance();

			*context.GetArgument<int*>(1) = vn ? vn->pearlColour : 0;
			*context.GetArgument<int*>(2) = vn ? vn->wheelColour : 0;
		}

		return 1;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_INTERIOR_COLOUR", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 1)
		{
			auto vn = entity->syncTree->GetVehicleAppearance();

			*context.GetArgument<int*>(1) = vn ? vn->interiorColour : 0;
		}

		return 1;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_DASHBOARD_COLOUR", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 1)
		{
			auto vn = entity->syncTree->GetVehicleAppearance();

			*context.GetArgument<int*>(1) = vn ? vn->dashboardColour : 0;
		}

		return 1;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_CUSTOM_PRIMARY_COLOUR", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 3)
		{
			auto vn = entity->syncTree->GetVehicleAppearance();

			*context.GetArgument<int*>(1) = (vn && vn->isPrimaryColourRGB) ? vn->primaryRedColour : 0;
			*context.GetArgument<int*>(2) = (vn && vn->isPrimaryColourRGB) ? vn->primaryGreenColour : 0;
			*context.GetArgument<int*>(3) = (vn && vn->isPrimaryColourRGB) ? vn->primaryBlueColour : 0;
		}

		return 1;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_CUSTOM_SECONDARY_COLOUR", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 3)
		{
			auto vn = entity->syncTree->GetVehicleAppearance();

			*context.GetArgument<int*>(1) = (vn && vn->isSecondaryColourRGB) ? vn->secondaryRedColour : 0;
			*context.GetArgument<int*>(2) = (vn && vn->isSecondaryColourRGB) ? vn->secondaryGreenColour : 0;
			*context.GetArgument<int*>(3) = (vn && vn->isSecondaryColourRGB) ? vn->secondaryBlueColour : 0;
		}

		return 1;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_IS_VEHICLE_PRIMARY_COLOUR_CUSTOM", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleAppearance();

		return vn ? vn->isPrimaryColourRGB : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_IS_VEHICLE_SECONDARY_COLOUR_CUSTOM", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleAppearance();

		return vn ? vn->isSecondaryColourRGB : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_LIVERY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleAppearance();

		return vn ? vn->liveryIndex : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_ROOF_LIVERY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleAppearance();

		return vn ? vn->roofLiveryIndex : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_DIRT_LEVEL", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleAppearance();

		return vn ? float(vn->dirtLevel) : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_WHEEL_TYPE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleAppearance();

		return vn ? vn->wheelType : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_WINDOW_TINT", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleAppearance();

		return (vn)
			? ((vn->windowTintIndex == 255) ? -1 : vn->windowTintIndex)
			: 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_TYRE_SMOKE_COLOR", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 2)
		{
			auto vn = entity->syncTree->GetVehicleAppearance();

			*context.GetArgument<int*>(1) = vn ? vn->tyreSmokeRedColour : 0;
			*context.GetArgument<int*>(2) = vn ? vn->tyreSmokeGreenColour : 0;
			*context.GetArgument<int*>(3) = vn ? vn->tyreSmokeBlueColour : 0;
		}

		return 1;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_NUMBER_PLATE_TEXT", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleAppearance();

		if (vn)
		{
			return (const char*)vn->plate;
		}
		
		return (const char*)"";
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_NUMBER_PLATE_TEXT_INDEX", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleAppearance();

		if (vn)
		{
			return vn->numberPlateTextIndex;
		}

		return 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("HAS_VEHICLE_BEEN_OWNED_BY_PLAYER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleGameState();

		if (vn)
		{
			return vn->hasBeenOwnedByPlayer;
		}

		return false;
	}));
	
	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_TOTAL_REPAIRS", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleHealth();

		if (vn)
		{
			return vn->totalRepairs;
		}

		return 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("HAS_ENTITY_BEEN_MARKED_AS_NO_LONGER_NEEDED", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		// GH-2203:
		// Since CScriptEntityExtension is not explicitly be synchronized, use
		// knowledge about CScriptEntityExtension's back-to-ambient conversion
		// as it cleans up an entities mission state. This approximation may
		// require refinement.
		bool result = entity->deleting;

		fx::sync::ePopType popType;
		if (entity->syncTree->GetPopulationType(&popType))
		{
			result |= popType == fx::sync::POPTYPE_RANDOM_AMBIENT;
		}

#if 0
		// Previous implementation
		if (auto vn = entity->syncTree->GetVehicleGameState())
		{
			result |= vn->isStationary;
		}
#endif

		return result;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_GAME_POOL", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::string_view poolName = context.CheckArgument<const char*>(0);

		std::vector<int> entityList;
		std::shared_lock l(gameState->m_entityListMutex);

		EntityType desiredType = EntityType::NoEntity;

		if (poolName == "CPed")
			desiredType = EntityType::Ped;	
		else if (poolName == "CVehicle")
			desiredType = EntityType::Vehicle;
		else if (poolName == "CObject" || poolName == "CNetObject")
			desiredType = EntityType::Object;

		for (auto& entity : gameState->m_entityList)
		{
			if (IsEntityValid(entity) && GetEntityType(entity) == desiredType)
			{
				entityList.push_back(gameState->MakeScriptHandle(entity));
			}
		}

		context.SetResult(fx::SerializeObject(entityList));
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_ALL_PEDS", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::vector<int> entityList;
		std::shared_lock<std::shared_mutex> lock(gameState->m_entityListMutex);

		for (auto& entity : gameState->m_entityList)
		{
			if (IsEntityValid(entity) && GetEntityType(entity) == EntityType::Ped)
			{
				entityList.push_back(gameState->MakeScriptHandle(entity));
			}
		}

		context.SetResult(fx::SerializeObject(entityList));
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_ALL_VEHICLES", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::vector<int> entityList;
		std::shared_lock<std::shared_mutex> lock(gameState->m_entityListMutex);

		for (auto& entity : gameState->m_entityList)
		{
			if (IsEntityValid(entity) && GetEntityType(entity) == EntityType::Vehicle)
			{
				entityList.push_back(gameState->MakeScriptHandle(entity));
			}
		}

		context.SetResult(fx::SerializeObject(entityList));
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_ALL_OBJECTS", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::vector<int> entityList;
		std::shared_lock<std::shared_mutex> lock(gameState->m_entityListMutex);

		for (auto& entity : gameState->m_entityList)
		{
			if (IsEntityValid(entity) && GetEntityType(entity) == EntityType::Object)
			{
				entityList.push_back(gameState->MakeScriptHandle(entity));
			}
		}

		context.SetResult(fx::SerializeObject(entityList));
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_PED_IS_IN", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		 bool lastVehicleArg = context.GetArgument<bool>(1);

		 // get the current resource manager
		 auto resourceManager = fx::ResourceManager::GetCurrent();

		 // get the owning server instance
		 auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		 // get the server's game state
		 auto gameState = instance->GetComponent<fx::ServerGameState>();

		 int lastVeh = 0;
		 int curVeh = 0;

#ifdef STATE_RDR3
		 auto pedVehicleData = entity->syncTree->GetPedVehicleData();
		 if (!pedVehicleData)
			 return (uint32_t)0;

		 if ((lastVehicleArg == true && pedVehicleData->lastVehiclePedWasIn == 0) || (lastVehicleArg == false && pedVehicleData->curVehicle == 0))
			 return (uint32_t)0;

		 lastVeh = pedVehicleData->lastVehiclePedWasIn;
		 curVeh = pedVehicleData->curVehicle;

#else
		 auto node = entity->syncTree->GetPedGameState();
		 if (!node)
			 return (uint32_t)0;

		 // If ped is not in a vehicle, or was not in a previous vehicle (depending on the lastVehicleArg) return 0
		 if ((lastVehicleArg == true && node->lastVehiclePedWasIn == -1) || (lastVehicleArg == false && node->curVehicle == -1))
			 return (uint32_t)0;

		 lastVeh = node->lastVehiclePedWasIn;
		 curVeh = node->curVehicle;
#endif

		 auto returnEntity = lastVehicleArg == true ? gameState->GetEntity(0, lastVeh) : gameState->GetEntity(0, curVeh);

		 if (!returnEntity)
			 return (uint32_t)0;

		 // Return the entity
		 return gameState->MakeScriptHandle(returnEntity);
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_IN_VEHICLE_SEAT", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
    {
        auto vn = entity->syncTree->GetVehicleGameState();

		const int seatArg = context.GetArgument<int>(1) + 2;
		if (seatArg < 0 || seatArg > 31)
		{
			return 0;
		}

        // get the current resource manager
        auto resourceManager = fx::ResourceManager::GetCurrent();

        // get the owning server instance
        auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

        // get the server's game state
        auto gameState = instance->GetComponent<fx::ServerGameState>();

        int retval = 0;

        if (vn && vn->occupants[seatArg]) 
        {
            auto pedEntity = gameState->GetEntity(0, vn->occupants[seatArg]);
            if (pedEntity) 
            {
                retval = gameState->MakeScriptHandle(pedEntity);
            }
        }

        return retval;
    }));

	fx::ScriptEngine::RegisterNativeHandler("GET_LAST_PED_IN_VEHICLE_SEAT", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
    {
        auto vn = entity->syncTree->GetVehicleGameState();

		const int seatArg = context.GetArgument<int>(1) + 2;
		if (seatArg < 0 || seatArg > 31)
		{
			return 0;
		}

        // get the current resource manager
        auto resourceManager = fx::ResourceManager::GetCurrent();

        // get the owning server instance
        auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

        // get the server's game state
        auto gameState = instance->GetComponent<fx::ServerGameState>();

        int retval = 0;

        if (vn && vn->lastOccupant[seatArg]) 
        {
            auto pedEntity = gameState->GetEntity(0, vn->lastOccupant[seatArg]);
            if (pedEntity) 
            {
                retval = gameState->MakeScriptHandle(pedEntity);
            }
        }

        return retval;
    }));

	fx::ScriptEngine::RegisterNativeHandler("DELETE_ENTITY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		gameState->DeleteEntity(entity);

		return 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("DELETE_TRAIN", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		// ignore the engine checks, this will recursively delete the entire train
		gameState->DeleteEntity<true>(entity);

		return 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_ENTITY_AS_NO_LONGER_NEEDED", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		// parse the client ID
		auto id = context.CheckArgument<uint32_t*>(0);

		if (!*id)
		{
			return;
		}

		auto entity = gameState->GetEntity(*id);

		if (!entity)
		{
			throw std::runtime_error(va("Tried to access invalid entity: %d", *id));
			return;
		}

		if (entity->GetClient())
		{
			// TODO: client-side set-as-no-longer-needed indicator
		}
		else
		{
			fx::DisownEntityScript(entity);
		}

		*id = 0;
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_SELECTED_PED_WEAPON", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto node = entity->syncTree->GetPedGameState();

		return uint32_t(node ? node->curWeapon : 0);
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_CURRENT_PED_WEAPON", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto node = entity->syncTree->GetPedGameState();

		return uint32_t(node ? node->curWeapon : 0);
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_PED_A_PLAYER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return entity->type == fx::sync::NetObjEntityType::Player;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_TEAM", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPlayerGameState();

		return pn ? pn->playerTeam : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_AIR_DRAG_MULTIPLIER_FOR_PLAYERS_VEHICLE", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPlayerGameState();

		return pn ? pn->airDragMultiplier : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_MAX_HEALTH", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPlayerGameState();

		return pn ? pn->maxHealth : 100;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_MAX_ARMOUR", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPlayerGameState();

		return pn ? pn->maxArmour : 100;
	}));

	fx::ScriptEngine::RegisterNativeHandler("NETWORK_GET_VOICE_PROXIMITY_OVERRIDE_FOR_PLAYER", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		float position[3];
		auto pn = entity->syncTree->GetPlayerGameState();

		scrVector resultVec = { 0 };

		if (pn)
		{
			resultVec.x = pn->voiceProximityOverrideX;
			resultVec.y = pn->voiceProximityOverrideY;
			resultVec.z = pn->voiceProximityOverrideZ;
		}
		else
		{
			resultVec.x = 0.0f;
			resultVec.y = 0.0f;
			resultVec.z = 0.0f;
		}

		return resultVec;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_WEAPON_DEFENSE_MODIFIER", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPlayerGameState();

		return pn ? pn->weaponDefenseModifier : 1.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_WEAPON_DEFENSE_MODIFIER_2", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPlayerGameState();

		return pn ? pn->weaponDefenseModifier2 : 1.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_WEAPON_DAMAGE_MODIFIER", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPlayerGameState();

		return pn ? pn->weaponDamageModifier : 1.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_MELEE_WEAPON_DAMAGE_MODIFIER", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPlayerGameState();

		return pn ? pn->meleeWeaponDamageModifier : 1.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_PLAYER_USING_SUPER_JUMP", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPlayerGameState();

		return pn ? pn->isSuperJumpEnabled : false;
	}));
	
	fx::ScriptEngine::RegisterNativeHandler("IS_VEHICLE_EXTRA_TURNED_ON", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto vn = entity->syncTree->GetVehicleAppearance();
		bool isExtraTurnedOn = false;

		if (context.GetArgumentCount() > 1 && vn)
		{
			// TODO: return false if the extra does not exist;

			int extraId = context.GetArgument<int>(1);
			isExtraTurnedOn = ((1 << (extraId + 1)) & vn->extras) == 0;
		}

		return isExtraTurnedOn;
	}));

	fx::ScriptEngine::RegisterNativeHandler("ENSURE_ENTITY_STATE_BAG", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (!entity->HasStateBag())
		{
			// get the current resource manager
			auto resourceManager = fx::ResourceManager::GetCurrent();

			// get the state bag component
			auto stateBagComponent = resourceManager->GetComponent<fx::StateBagComponent>();

			auto stateBag = stateBagComponent->RegisterStateBag(fmt::sprintf("entity:%d", entity->handle & 0xFFFF));

			std::set<int> rts{ -1 };

			for (auto i = entity->relevantTo.find_first(); i != decltype(entity->relevantTo)::kSize; i = entity->relevantTo.find_next(i))
			{
				rts.insert(i);
			}

			stateBag->SetRoutingTargets(rts);

			auto client = entity->GetClient();

			if (client)
			{
				stateBag->SetOwningPeer(client->GetSlotId());
			}
			else
			{
				stateBag->SetOwningPeer(-1);
			}

			entity->SetStateBag(std::move(stateBag));
		}

		return 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_ENTITY_DISTANCE_CULLING_RADIUS", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 1)
		{
			float radius = context.GetArgument<float>(1);
			entity->overrideCullingRadius = radius * radius;
		}

		return true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_ENTITY_IGNORE_REQUEST_CONTROL_FILTER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 1)
		{
			bool ignore = context.GetArgument<bool>(1);
			entity->ignoreRequestControlFilter = ignore;
		}

		return true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_ROUTING_BUCKET", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		auto [lock, clientData] = gameState->ExternalGetClientData(client);
		return int(clientData->routingBucket);
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_PLAYER_ROUTING_BUCKET", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		if (context.GetArgumentCount() > 1)
		{   
			const char* player = context.GetArgument<char*>(0);
			auto bucket = context.GetArgument<int>(1);

			if (bucket >= 0)
			{
				// get the current resource manager
				auto resourceManager = fx::ResourceManager::GetCurrent();

				// get the owning server instance
				auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

				// get the server's game state
				auto gameState = instance->GetComponent<fx::ServerGameState>();

				auto [lock, clientData] = gameState->ExternalGetClientData(client);

				 // store old bucket for event
				const auto oldBucket = clientData->routingBucket;

				gameState->ClearClientFromWorldGrid(client);
				clientData->routingBucket = bucket;
				
				fx::sync::SyncEntityPtr playerEntity;

				{
					std::shared_lock _lock(clientData->playerEntityMutex);
					playerEntity = clientData->playerEntity.lock();
				}

				if (playerEntity)
				{
					playerEntity->routingBucket = bucket;
				}

				
				auto eventManager = resourceManager->GetComponent<fx::ResourceEventManagerComponent>();
				/*NETEV onPlayerBucketChange SERVER
				/#*
				 * Triggered when a routing bucket changed for a player on the server.
				 *
				 * @param player - The id of the player that changed bucket.
				 * @param bucket - The new bucket that is placing the player into.
				 * @param oldBucket - The old bucket where the player was previously in.
				 *
				 #/
				  declare function onPlayerBucketChange(player: string, bucket: number, oldBucket: number): void;
				*/
				eventManager->TriggerEvent2("onPlayerBucketChange", {}, player, bucket, oldBucket);
			}
		}

		return true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_ROUTING_BUCKET", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return int(entity->routingBucket);
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_ENTITY_ROUTING_BUCKET", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 1)
		{
			const auto ent = context.GetArgument<uint32_t>(0);
			auto bucket = context.GetArgument<int>(1);
		 	int oldBucket = entity->routingBucket;

			if (bucket >= 0)
			{
				entity->routingBucket = bucket;
			}

			auto resourceManager = fx::ResourceManager::GetCurrent();
			auto eventManager = resourceManager->GetComponent<fx::ResourceEventManagerComponent>();
			/*NETEV onEntityBucketChange SERVER
			/#*
			 * Triggered when a routing bucket changed for an entity on the server.
			 *
			 * @param entity - The entity id that changed bucket.
			 * @param bucket - The new bucket that is placing the entity into.
			 * @param oldBucket - The old bucket where the entity was previously in.
			 *
			#/
			  declare function onEntityBucketChange(entity: string, bucket: number, oldBucket: number): void;
			*/
			eventManager->TriggerEvent2("onEntityBucketChange", {}, ent, bucket, oldBucket);
		}

		return true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_INVINCIBLE", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPlayerGameState();

		return pn ? pn->isInvincible : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_CAMERA_ROTATION", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto camData = entity->syncTree->GetPlayerCamera();

		scrVector resultVector = { 0 };

		if (camData)
		{
			resultVector.x = camData->cameraX;
			resultVector.y = 0.0f;
			resultVector.z = camData->cameraZ;
		}
		else
		{
			resultVector.x = 0.0f;
			resultVector.y = 0.0f;
			resultVector.z = 0.0f;
		}

		return resultVector;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_PLAYER_IN_FREE_CAM_MODE", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (const auto& syncTree = entity->syncTree)
		{
			if (const auto camData = syncTree->GetPlayerCamera(); camData->camMode != 0)
			{
				return true;
			}
		}

		return false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_FOCUS_POS", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		scrVector resultVec = {};
		const auto& syncTree = entity->syncTree;

		if (!syncTree)
		{
			return resultVec;
		}

		const auto camData = syncTree->GetPlayerCamera();

		if (!camData)
		{
			return resultVec;
		}

		float playerPos[3];
		syncTree->GetPosition(playerPos);

		switch (camData->camMode)
		{
			case 0:
			default:
				resultVec.x = playerPos[0];
				resultVec.y = playerPos[1];
				resultVec.z = playerPos[2];
				break;
			case 1:
				resultVec.x = camData->freeCamPosX;
				resultVec.y = camData->freeCamPosY;
				resultVec.z = camData->freeCamPosZ;
				break;
			case 2:
				resultVec.x = playerPos[0] + camData->camOffX;
				resultVec.y = playerPos[1] + camData->camOffY;
				resultVec.z = playerPos[2] + camData->camOffZ;
				break;
		}

		return resultVec;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_TRAIN_CARRIAGE_ENGINE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto train = entity->syncTree->GetTrainState();

		if (!train)
		{
			return uint32_t(0);
		}

		auto resourceManager = fx::ResourceManager::GetCurrent();

		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		auto gameState = instance->GetComponent<fx::ServerGameState>();

		auto engine = gameState->GetEntity(0, train->engineCarriage);

		return engine ? gameState->MakeScriptHandle(engine) : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_TRAIN_CARRIAGE_INDEX", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto train = entity->syncTree->GetTrainState();

		return train ? train->carriageIndex : -1;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_TRAIN_STATE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto train = entity->syncTree->GetTrainState();

		return train ? train->trainState : -1;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_TRAIN_CABOOSE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto train = entity->syncTree->GetTrainState();

		return train ? train->isCaboose : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("DOES_TRAIN_STOP_AT_STATIONS", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto train = entity->syncTree->GetTrainState();

		return train ? train->stopAtStations : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_TRAIN_CRUISE_SPEED", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto train = entity->syncTree->GetTrainState();

		return train ? train->cruiseSpeed : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_TRAIN_TRACK_INDEX", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto train = entity->syncTree->GetTrainState();

		return train ? train->trackId : -1;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_TRAIN_FORWARD_CARRIAGE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto train = entity->syncTree->GetTrainState();

		if (!train)
		{
			return uint32_t(0);
		}

		if (train->isEngine)
		{
			return uint32_t(0);
		}

		auto resourceManager = fx::ResourceManager::GetCurrent();

		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		auto gameState = instance->GetComponent<fx::ServerGameState>();

		auto forwardCarriage = gameState->GetEntity(0, train->linkedToForwardId);

		return forwardCarriage ? gameState->MakeScriptHandle(forwardCarriage) : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_TRAIN_BACKWARD_CARRIAGE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto train = entity->syncTree->GetTrainState();

		if (!train)
		{
			return uint32_t(0);
		}

		if (train->isCaboose)
		{
			return uint32_t(0);
		}

		auto resourceManager = fx::ResourceManager::GetCurrent();

		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		auto gameState = instance->GetComponent<fx::ServerGameState>();

		auto backwardCarriage = gameState->GetEntity(0, train->linkedToBackwardId);

		return backwardCarriage ? gameState->MakeScriptHandle(backwardCarriage) : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_TRAIN_DIRECTION", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto train = entity->syncTree->GetTrainState();

		return train ? train->direction : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_FAKE_WANTED_LEVEL", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPlayerWantedAndLOS();

		return pn ? pn->fakeWantedLevel : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_WANTED_CENTRE_POSITION", MakePlayerEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto pn = entity->syncTree->GetPlayerWantedAndLOS();

		scrVector resultVector = { 0 };

		if (pn)
		{
			resultVector.x = pn->wantedPositionX;
			resultVector.y = pn->wantedPositionY;
			resultVector.z = pn->wantedPositionZ;
		}
		else
		{
			resultVector.x = 0.0f;
			resultVector.y = 0.0f;
			resultVector.z = 0.0f;
		}

		return resultVector;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_ENTITY_VISIBLE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		bool visible = false;
		entity->syncTree->IsEntityVisible(&visible);

		return visible;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_SOURCE_OF_DAMAGE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto node = entity->syncTree->GetPedHealth();


		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		if (!node || node->sourceOfDamage == 0)
			return (uint32_t)0;

		auto returnEntity = gameState->GetEntity(0, node->sourceOfDamage);

		if (!returnEntity)
			return (uint32_t)0;

		// Return the entity
		return gameState->MakeScriptHandle(returnEntity);
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_SOURCE_OF_DEATH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto node = entity->syncTree->GetPedHealth();


		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		if (!node || node->health > 0 || node->sourceOfDamage == 0)
			return (uint32_t)0;

		auto returnEntity = gameState->GetEntity(0, node->sourceOfDamage);

		if (!returnEntity)
			return (uint32_t)0;

		// Return the entity
		return gameState->MakeScriptHandle(returnEntity);
	}));
  
	fx::ScriptEngine::RegisterNativeHandler("SET_PLAYER_CULLING_RADIUS", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		if (context.GetArgumentCount() > 1)
		{
			float radius = context.GetArgument<float>(1);

			if (radius >= 0)
			{
				// get the current resource manager
				auto resourceManager = fx::ResourceManager::GetCurrent();

				// get the owning server instance
				auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

				// get the server's game state
				auto gameState = instance->GetComponent<fx::ServerGameState>();

				auto [lock, clientData] = gameState->ExternalGetClientData(client);
				clientData->playerCullingRadius = radius * radius;
			}
		}

		return true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_PLAYER_HIDDEN_FROM_OTHERS", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		if (context.GetArgumentCount() > 1)
		{
			bool hidden = context.GetArgument<bool>(1);
			auto resourceManager = fx::ResourceManager::GetCurrent();
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
			auto gameState = instance->GetComponent<fx::ServerGameState>();
			gameState->SetPlayerHiddenFromOthers(client->GetNetId(), hidden);
		}

		return true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_PLAYER_HIDDEN_FROM_OTHERS", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		return gameState->IsPlayerHiddenFromOthers(client->GetNetId());
	}, false));

	fx::ScriptEngine::RegisterNativeHandler("SET_VEHICLE_DOORS_SERVER_AUTHORITATIVE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		bool toggle = context.GetArgument<bool>(1);
		entity->doorsServerAuthoritative = toggle;

		if (toggle)
		{
			if (auto vgs = entity->syncTree->GetVehicleGameState())
			{
				entity->authoritativeLockStatus = vgs->lockStatus;
			}
			else
			{
				entity->authoritativeLockStatus = 2; // default locked
			}
		}
		return 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_VEHICLE_DOORS_SERVER_AUTHORITATIVE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return entity->doorsServerAuthoritative;
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_VEHICLE_DOOR_LOCK_STATUS_AUTHORITATIVE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		int lockStatus = context.GetArgument<int>(1);
		entity->doorsServerAuthoritative = true;
		entity->authoritativeLockStatus = lockStatus;

		if (auto vgs = entity->syncTree->GetVehicleGameState())
		{
			vgs->lockStatus = lockStatus;
			if (lockStatus > 1)
			{
				vgs->doorsOpen = 0;
				memset(vgs->doorPositions, 0, sizeof(vgs->doorPositions));
			}
		}
		return 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("SET_PLAYER_MAX_VELOCITY", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		if (context.GetArgumentCount() > 1)
		{
			float maxVelocity = context.GetArgument<float>(1);

			auto resourceManager = fx::ResourceManager::GetCurrent();
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
			auto gameState = instance->GetComponent<fx::ServerGameState>();
			gameState->SetPlayerMaxVelocity(client->GetNetId(), maxVelocity);

			auto [lock, clientData] = gameState->ExternalGetClientData(client);
			if (clientData)
			{
				std::shared_lock _lock(clientData->playerEntityMutex);
				if (auto playerEntity = clientData->playerEntity.lock())
				{
					playerEntity->maxVelocity = maxVelocity;
				}
			}
		}

		return true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_MAX_VELOCITY", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		return gameState->GetPlayerMaxVelocity(client->GetNetId());
	}, 0.0f));

	fx::ScriptEngine::RegisterNativeHandler("SET_ENTITY_MAX_VELOCITY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		if (context.GetArgumentCount() > 1)
		{
			float maxVelocity = context.GetArgument<float>(1);
			entity->maxVelocity = maxVelocity;
		}
		return 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_MAX_VELOCITY", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return entity->maxVelocity;
	}));



	fx::ScriptEngine::RegisterNativeHandler("NETWORK_FORCE_ENTITY_OWNER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		int playerSrc = context.GetArgument<int>(1);
		if (playerSrc > 0)
		{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto clientRegistry = instance->GetComponent<fx::ClientRegistry>();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		if (auto client = clientRegistry->GetClientByNetID(playerSrc))
		{
			entity->forcedOwnerNetId = client->GetNetId();
			gameState->ReassignEntity(entity->handle, client);
			return true;
		}

		return false;
	}
	else
	{
		entity->forcedOwnerNetId = 0;
		return true;
	}
	}));



	fx::ScriptEngine::RegisterNativeHandler("NETWORK_GET_FORCED_ENTITY_OWNER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return (entity->forcedOwnerNetId != 0) ? int(entity->forcedOwnerNetId) : -1;
	}));



	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_ATTACHED_ENTITIES", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& parentEntity)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::vector<int> attachedHandles;
		uint16_t parentObjId = parentEntity->handle & 0x1FFF;

		std::shared_lock elm(gameState->m_entityListMutex);
		for (const auto& child : gameState->m_entityList)
		{
			if (!child || child == parentEntity || child->deleting)
			{
				continue;
			}

			if (auto syncTree = child->syncTree)
			{
				if (auto attachment = syncTree->GetAttachment())
				{
					if (attachment->attached && (attachment->attachedTo == parentObjId || attachment->attachedTo == (parentEntity->handle & 0x1FFF)))
					{
						attachedHandles.push_back(gameState->MakeScriptHandle(child));
					}
				}
			}
		}

		return (const char*)SerializeIntListToJson(attachedHandles);
	}));

	
	//
	// Custom Server Natives: Fast Spatial Queries & Authoritative Sync Suite
	//

	// GET_PLAYERS_IN_RADIUS(float x, float y, float z, float radius, [optional] int routingBucket = -1) -> table
	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYERS_IN_RADIUS", [](fx::ScriptContext& context)
	{
		float x = context.GetArgument<float>(0);
		float y = context.GetArgument<float>(1);
		float z = context.GetArgument<float>(2);
		float radius = context.GetArgument<float>(3);
		int filterBucket = context.GetArgumentCount() > 4 ? context.GetArgument<int>(4) : -1;

		float radiusSq = radius * radius;

		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		auto clientRegistry = instance->GetComponent<fx::ClientRegistry>();

		std::vector<int> playerList;

		clientRegistry->ForAllClients([&](const fx::ClientSharedPtr& client)
		{
			if (!client || !client->HasSlotId())
			{
				return;
			}

			fx::sync::SyncEntityPtr playerEntity;
			{
				auto [lock, data] = gameState->ExternalGetClientData(client);
				playerEntity = data->playerEntity.lock();
			}

			if (!playerEntity || !playerEntity->syncTree)
			{
				return;
			}

			if (filterBucket >= 0 && playerEntity->routingBucket != (uint32_t)filterBucket)
			{
				return;
			}

			float pos[3];
			playerEntity->syncTree->GetPosition(pos);

			float dx = pos[0] - x;
			float dy = pos[1] - y;
			float dz = pos[2] - z;

			if (dx * dx + dy * dy + dz * dz <= radiusSq)
			{
				playerList.push_back(client->GetNetId());
			}
		});

		context.SetResult<const char*>(SerializeIntListToJson(playerList));
	});

	// GET_CLOSEST_PLAYER(float x, float y, float z, float maxRadius, [optional] int routingBucket = -1) -> int
	fx::ScriptEngine::RegisterNativeHandler("GET_CLOSEST_PLAYER", [](fx::ScriptContext& context)
	{
		float x = context.GetArgument<float>(0);
		float y = context.GetArgument<float>(1);
		float z = context.GetArgument<float>(2);
		float maxRadius = context.GetArgument<float>(3);
		int filterBucket = context.GetArgumentCount() > 4 ? context.GetArgument<int>(4) : -1;

		float minDistanceSq = maxRadius * maxRadius;
		int closestPlayer = 0;

		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		auto clientRegistry = instance->GetComponent<fx::ClientRegistry>();

		clientRegistry->ForAllClients([&](const fx::ClientSharedPtr& client)
		{
			if (!client || !client->HasSlotId())
			{
				return;
			}

			fx::sync::SyncEntityPtr playerEntity;
			{
				auto [lock, data] = gameState->ExternalGetClientData(client);
				playerEntity = data->playerEntity.lock();
			}

			if (!playerEntity || !playerEntity->syncTree)
			{
				return;
			}

			if (filterBucket >= 0 && playerEntity->routingBucket != (uint32_t)filterBucket)
			{
				return;
			}

			float pos[3];
			playerEntity->syncTree->GetPosition(pos);

			float dx = pos[0] - x;
			float dy = pos[1] - y;
			float dz = pos[2] - z;
			float distSq = dx * dx + dy * dy + dz * dz;

			if (distSq <= minDistanceSq)
			{
				minDistanceSq = distSq;
				closestPlayer = client->GetNetId();
			}
		});

		context.SetResult(closestPlayer);
	});

	// GET_VEHICLES_IN_RADIUS(float x, float y, float z, float radius, [optional] int routingBucket = -1) -> table
	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLES_IN_RADIUS", [](fx::ScriptContext& context)
	{
		float x = context.GetArgument<float>(0);
		float y = context.GetArgument<float>(1);
		float z = context.GetArgument<float>(2);
		float radius = context.GetArgument<float>(3);
		int filterBucket = context.GetArgumentCount() > 4 ? context.GetArgument<int>(4) : -1;

		float radiusSq = radius * radius;

		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::vector<int> entityList;
		std::shared_lock<std::shared_mutex> lock(gameState->m_entityListMutex);

		for (auto& entity : gameState->m_entityList)
		{
			if (IsEntityValid(entity) && GetEntityType(entity) == EntityType::Vehicle && entity->syncTree)
			{
				if (filterBucket >= 0 && entity->routingBucket != (uint32_t)filterBucket)
				{
					continue;
				}

				float pos[3];
				entity->syncTree->GetPosition(pos);

				float dx = pos[0] - x;
				float dy = pos[1] - y;
				float dz = pos[2] - z;

				if (dx * dx + dy * dy + dz * dz <= radiusSq)
				{
					entityList.push_back(gameState->MakeScriptHandle(entity));
				}
			}
		}

		context.SetResult<const char*>(SerializeIntListToJson(entityList));
	});

	// GET_CLOSEST_VEHICLE(float x, float y, float z, float maxRadius, [optional] int routingBucket = -1) -> int
	fx::ScriptEngine::RegisterNativeHandler("GET_CLOSEST_VEHICLE", [](fx::ScriptContext& context)
	{
		float x = context.GetArgument<float>(0);
		float y = context.GetArgument<float>(1);
		float z = context.GetArgument<float>(2);
		float maxRadius = context.GetArgument<float>(3);
		int filterBucket = context.GetArgumentCount() > 4 ? context.GetArgument<int>(4) : -1;

		float minDistanceSq = maxRadius * maxRadius;
		fx::sync::SyncEntityPtr closestEntity;

		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::shared_lock<std::shared_mutex> lock(gameState->m_entityListMutex);

		for (auto& entity : gameState->m_entityList)
		{
			if (IsEntityValid(entity) && GetEntityType(entity) == EntityType::Vehicle && entity->syncTree)
			{
				if (filterBucket >= 0 && entity->routingBucket != (uint32_t)filterBucket)
				{
					continue;
				}

				float pos[3];
				entity->syncTree->GetPosition(pos);

				float dx = pos[0] - x;
				float dy = pos[1] - y;
				float dz = pos[2] - z;
				float distSq = dx * dx + dy * dy + dz * dz;

				if (distSq <= minDistanceSq)
				{
					minDistanceSq = distSq;
					closestEntity = entity;
				}
			}
		}

		context.SetResult(closestEntity ? gameState->MakeScriptHandle(closestEntity) : 0);
	});

	// GET_PEDS_IN_RADIUS(float x, float y, float z, float radius, [optional] int routingBucket = -1) -> table
	fx::ScriptEngine::RegisterNativeHandler("GET_PEDS_IN_RADIUS", [](fx::ScriptContext& context)
	{
		float x = context.GetArgument<float>(0);
		float y = context.GetArgument<float>(1);
		float z = context.GetArgument<float>(2);
		float radius = context.GetArgument<float>(3);
		int filterBucket = context.GetArgumentCount() > 4 ? context.GetArgument<int>(4) : -1;

		float radiusSq = radius * radius;

		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::vector<int> entityList;
		std::shared_lock<std::shared_mutex> lock(gameState->m_entityListMutex);

		for (auto& entity : gameState->m_entityList)
		{
			if (IsEntityValid(entity) && GetEntityType(entity) == EntityType::Ped && entity->syncTree)
			{
				if (filterBucket >= 0 && entity->routingBucket != (uint32_t)filterBucket)
				{
					continue;
				}

				float pos[3];
				entity->syncTree->GetPosition(pos);

				float dx = pos[0] - x;
				float dy = pos[1] - y;
				float dz = pos[2] - z;

				if (dx * dx + dy * dy + dz * dz <= radiusSq)
				{
					entityList.push_back(gameState->MakeScriptHandle(entity));
				}
			}
		}

		context.SetResult<const char*>(SerializeIntListToJson(entityList));
	});

	// GET_CLOSEST_PED(float x, float y, float z, float maxRadius, [optional] int routingBucket = -1) -> int
	fx::ScriptEngine::RegisterNativeHandler("GET_CLOSEST_PED", [](fx::ScriptContext& context)
	{
		float x = context.GetArgument<float>(0);
		float y = context.GetArgument<float>(1);
		float z = context.GetArgument<float>(2);
		float maxRadius = context.GetArgument<float>(3);
		int filterBucket = context.GetArgumentCount() > 4 ? context.GetArgument<int>(4) : -1;

		float minDistanceSq = maxRadius * maxRadius;
		fx::sync::SyncEntityPtr closestEntity;

		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::shared_lock<std::shared_mutex> lock(gameState->m_entityListMutex);

		for (auto& entity : gameState->m_entityList)
		{
			if (IsEntityValid(entity) && GetEntityType(entity) == EntityType::Ped && entity->syncTree)
			{
				if (filterBucket >= 0 && entity->routingBucket != (uint32_t)filterBucket)
				{
					continue;
				}

				float pos[3];
				entity->syncTree->GetPosition(pos);

				float dx = pos[0] - x;
				float dy = pos[1] - y;
				float dz = pos[2] - z;
				float distSq = dx * dx + dy * dy + dz * dz;

				if (distSq <= minDistanceSq)
				{
					minDistanceSq = distSq;
					closestEntity = entity;
				}
			}
		}

		context.SetResult(closestEntity ? gameState->MakeScriptHandle(closestEntity) : 0);
	});

	// GET_OBJECTS_IN_RADIUS(float x, float y, float z, float radius, [optional] int routingBucket = -1) -> table
	fx::ScriptEngine::RegisterNativeHandler("GET_OBJECTS_IN_RADIUS", [](fx::ScriptContext& context)
	{
		float x = context.GetArgument<float>(0);
		float y = context.GetArgument<float>(1);
		float z = context.GetArgument<float>(2);
		float radius = context.GetArgument<float>(3);
		int filterBucket = context.GetArgumentCount() > 4 ? context.GetArgument<int>(4) : -1;

		float radiusSq = radius * radius;

		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::vector<int> entityList;
		std::shared_lock<std::shared_mutex> lock(gameState->m_entityListMutex);

		for (auto& entity : gameState->m_entityList)
		{
			if (IsEntityValid(entity) && GetEntityType(entity) == EntityType::Object && entity->syncTree)
			{
				if (filterBucket >= 0 && entity->routingBucket != (uint32_t)filterBucket)
				{
					continue;
				}

				float pos[3];
				entity->syncTree->GetPosition(pos);

				float dx = pos[0] - x;
				float dy = pos[1] - y;
				float dz = pos[2] - z;

				if (dx * dx + dy * dy + dz * dz <= radiusSq)
				{
					entityList.push_back(gameState->MakeScriptHandle(entity));
				}
			}
		}

		context.SetResult<const char*>(SerializeIntListToJson(entityList));
	});

	// GET_CLOSEST_OBJECT(float x, float y, float z, float maxRadius, [optional] int routingBucket = -1) -> int
	fx::ScriptEngine::RegisterNativeHandler("GET_CLOSEST_OBJECT", [](fx::ScriptContext& context)
	{
		float x = context.GetArgument<float>(0);
		float y = context.GetArgument<float>(1);
		float z = context.GetArgument<float>(2);
		float maxRadius = context.GetArgument<float>(3);
		int filterBucket = context.GetArgumentCount() > 4 ? context.GetArgument<int>(4) : -1;

		float minDistanceSq = maxRadius * maxRadius;
		fx::sync::SyncEntityPtr closestEntity;

		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		std::shared_lock<std::shared_mutex> lock(gameState->m_entityListMutex);

		for (auto& entity : gameState->m_entityList)
		{
			if (IsEntityValid(entity) && GetEntityType(entity) == EntityType::Object && entity->syncTree)
			{
				if (filterBucket >= 0 && entity->routingBucket != (uint32_t)filterBucket)
				{
					continue;
				}

				float pos[3];
				entity->syncTree->GetPosition(pos);

				float dx = pos[0] - x;
				float dy = pos[1] - y;
				float dz = pos[2] - z;
				float distSq = dx * dx + dy * dy + dz * dz;

				if (distSq <= minDistanceSq)
				{
					minDistanceSq = distSq;
					closestEntity = entity;
				}
			}
		}

		context.SetResult(closestEntity ? gameState->MakeScriptHandle(closestEntity) : 0);
	});

	// SET_VEHICLE_DOOR_OPEN_AUTHORITATIVE(veh, doorIndex, loose, openInstantly) -> bool
	fx::ScriptEngine::RegisterNativeHandler("SET_VEHICLE_DOOR_OPEN_AUTHORITATIVE", [](fx::ScriptContext& context)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		auto id = context.GetArgument<uint32_t>(0);
		if (id)
		{
			if (auto entity = gameState->GetEntity(id))
			{
				int doorIndex = context.GetArgument<int>(1);
				if (doorIndex >= 0 && doorIndex < 7)
				{
					entity->doorsServerAuthoritative = true;
					entity->authoritativeDoorsOpen |= (1 << doorIndex);
					entity->authoritativeDoorPositions[doorIndex] = 7;

					if (auto vgs = entity->syncTree->GetVehicleGameState())
					{
						vgs->doorsOpen |= (1 << doorIndex);
						vgs->doorPositions[doorIndex] = 7;
					}
				}
			}
		}

		auto rpcHandler = fx::ScriptEngine::GetNativeHandler(HashString("SET_VEHICLE_DOOR_OPEN"));
		if (rpcHandler)
		{
			rpcHandler(context);
		}
		context.SetResult(true);
	});

	// SET_VEHICLE_DOOR_SHUT_AUTHORITATIVE(veh, doorIndex, closeInstantly) -> bool
	fx::ScriptEngine::RegisterNativeHandler("SET_VEHICLE_DOOR_SHUT_AUTHORITATIVE", [](fx::ScriptContext& context)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		auto id = context.GetArgument<uint32_t>(0);
		if (id)
		{
			if (auto entity = gameState->GetEntity(id))
			{
				int doorIndex = context.GetArgument<int>(1);
				if (doorIndex >= 0 && doorIndex < 7)
				{
					entity->doorsServerAuthoritative = true;
					entity->authoritativeDoorsOpen &= ~(1 << doorIndex);
					entity->authoritativeDoorPositions[doorIndex] = 0;

					if (auto vgs = entity->syncTree->GetVehicleGameState())
					{
						vgs->doorsOpen &= ~(1 << doorIndex);
						vgs->doorPositions[doorIndex] = 0;
					}
				}
			}
		}

		auto rpcHandler = fx::ScriptEngine::GetNativeHandler(HashString("SET_VEHICLE_DOOR_SHUT"));
		if (rpcHandler)
		{
			rpcHandler(context);
		}
		context.SetResult(true);
	});

	// SET_VEHICLE_ENGINE_HEALTH_AUTHORITATIVE(veh, health) -> bool
	fx::ScriptEngine::RegisterNativeHandler("SET_VEHICLE_ENGINE_HEALTH_AUTHORITATIVE", [](fx::ScriptContext& context)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		auto id = context.GetArgument<uint32_t>(0);
		if (id)
		{
			if (auto entity = gameState->GetEntity(id))
			{
				float health = context.GetArgument<float>(1);
				entity->engineHealthAuthoritative = true;
				entity->authoritativeEngineHealth = health;

				if (auto vn = entity->syncTree->GetVehicleHealth())
				{
					vn->engineHealth = (int)health;
				}
			}
		}

		auto rpcHandler = fx::ScriptEngine::GetNativeHandler(HashString("SET_VEHICLE_ENGINE_HEALTH"));
		if (rpcHandler)
		{
			rpcHandler(context);
		}
		context.SetResult(true);
	});

	// GET_VEHICLE_ENGINE_HEALTH_AUTHORITATIVE(veh) -> float
	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_ENGINE_HEALTH_AUTHORITATIVE", [](fx::ScriptContext& context)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		auto id = context.GetArgument<uint32_t>(0);
		if (!id)
		{
			context.SetResult(1000.0f);
			return;
		}
		auto entity = gameState->GetEntity(id);
		if (!entity)
		{
			context.SetResult(1000.0f);
			return;
		}
		if (entity->engineHealthAuthoritative)
		{
			context.SetResult(entity->authoritativeEngineHealth);
			return;
		}
		if (auto vn = entity->syncTree->GetVehicleHealth())
		{
			context.SetResult((float)vn->engineHealth);
			return;
		}
		context.SetResult(1000.0f);
	});

	// SET_VEHICLE_TYRE_BURST_AUTHORITATIVE(veh, tyreIndex, completely) -> bool
	fx::ScriptEngine::RegisterNativeHandler("SET_VEHICLE_TYRE_BURST_AUTHORITATIVE", [](fx::ScriptContext& context)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		auto id = context.GetArgument<uint32_t>(0);
		int tyreIndex = context.GetArgument<int>(1);
		bool completely = context.GetArgumentCount() > 2 ? context.GetArgument<bool>(2) : true;

		if (id && tyreIndex >= 0 && tyreIndex < 16)
		{
			if (auto entity = gameState->GetEntity(id))
			{
				entity->tyresAuthoritative = true;
				entity->authoritativeTyresFine = false;
				entity->authoritativeTyreStatus[tyreIndex] = completely ? 2 : 1;

				if (auto vn = entity->syncTree->GetVehicleHealth())
				{
					vn->tyresFine = false;
					vn->tyreStatus[tyreIndex] = completely ? 2 : 1;
				}
			}
		}

		auto rpcHandler = fx::ScriptEngine::GetNativeHandler(HashString("SET_VEHICLE_TYRE_BURST"));
		if (rpcHandler)
		{
			fx::ScriptContextBuffer rpcCtx;
			rpcCtx.Push(id);
			rpcCtx.Push(tyreIndex);
			rpcCtx.Push(completely);
			rpcCtx.Push(1000.0f);
			rpcHandler(rpcCtx);
		}
		context.SetResult(true);
	});

	// SET_VEHICLE_TYRE_FIXED_AUTHORITATIVE(veh, tyreIndex) -> bool
	fx::ScriptEngine::RegisterNativeHandler("SET_VEHICLE_TYRE_FIXED_AUTHORITATIVE", [](fx::ScriptContext& context)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		auto id = context.GetArgument<uint32_t>(0);
		int tyreIndex = context.GetArgument<int>(1);

		if (id && tyreIndex >= 0 && tyreIndex < 16)
		{
			if (auto entity = gameState->GetEntity(id))
			{
				entity->authoritativeTyreStatus[tyreIndex] = 0;
				bool anyBurst = false;
				for (int i = 0; i < 16; i++)
				{
					if (entity->authoritativeTyreStatus[i] != 0)
					{
						anyBurst = true;
						break;
					}
				}
				entity->authoritativeTyresFine = !anyBurst;

			if (auto vn = entity->syncTree->GetVehicleHealth())
				{
					vn->tyreStatus[tyreIndex] = 0;
					vn->tyresFine = !anyBurst;
				}
			}
		}

		auto rpcHandler = fx::ScriptEngine::GetNativeHandler(HashString("SET_VEHICLE_TYRE_FIXED"));
		if (rpcHandler)
		{
			rpcHandler(context);
		}
		context.SetResult(true);
	});

	// SET_VEHICLE_DOOR_BROKEN_AUTHORITATIVE(veh, doorIndex, broken) -> bool
	fx::ScriptEngine::RegisterNativeHandler("SET_VEHICLE_DOOR_BROKEN_AUTHORITATIVE", [](fx::ScriptContext& context)
	{
		auto handler = fx::ScriptEngine::GetNativeHandler(HashString("SET_VEHICLE_DOOR_BROKEN"));
		if (handler)
		{
			handler(context);
			context.SetResult(true);
			return;
		}
		context.SetResult(false);
	});

	// SET_VEHICLE_NUMBER_PLATE_TEXT_AUTHORITATIVE(veh, plateText) -> bool
	fx::ScriptEngine::RegisterNativeHandler("SET_VEHICLE_NUMBER_PLATE_TEXT_AUTHORITATIVE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		const char* plateText = context.CheckArgument<const char*>(1);
		if (!plateText)
		{
			return false;
		}

		entity->plateAuthoritative = true;
		memset(entity->authoritativePlate, ' ', 8);
		size_t len = strlen(plateText);
		if (len > 8) len = 8;
		memcpy(entity->authoritativePlate, plateText, len);
		entity->authoritativePlate[8] = '\0';

		if (auto va = entity->syncTree->GetVehicleAppearance())
		{
			va->hasPlate = true;
			memcpy(va->plate, entity->authoritativePlate, 8);
			va->plate[8] = '\0';
		}

		auto handler = fx::ScriptEngine::GetNativeHandler(HashString("SET_VEHICLE_NUMBER_PLATE_TEXT"));
		if (handler)
		{
			handler(context);
		}

		return true;
	}, false));

	// SET_PLAYER_INVINCIBLE_SERVER(playerSrc, toggle) -> bool
	fx::ScriptEngine::RegisterNativeHandler("SET_PLAYER_INVINCIBLE_SERVER", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		bool toggle = context.GetArgument<bool>(1);
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		gameState->SetPlayerInvincible(client->GetNetId(), toggle);

		auto handler = fx::ScriptEngine::GetNativeHandler(HashString("SET_PLAYER_INVINCIBLE"));
		if (handler)
		{
			handler(context);
		}

		return true;
	}, false));

	// IS_PLAYER_INVINCIBLE_SERVER(playerSrc) -> bool
	fx::ScriptEngine::RegisterNativeHandler("IS_PLAYER_INVINCIBLE_SERVER", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		return gameState->IsPlayerInvincible(client->GetNetId());
	}, false));

	// SET_ENTITY_INVINCIBLE_SERVER(entity, toggle) -> bool
	fx::ScriptEngine::RegisterNativeHandler("SET_ENTITY_INVINCIBLE_SERVER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		bool toggle = context.GetArgument<bool>(1);
		entity->isInvincible = toggle;
		return true;
	}, false));

	// IS_ENTITY_INVINCIBLE_SERVER(entity) -> bool
	fx::ScriptEngine::RegisterNativeHandler("IS_ENTITY_INVINCIBLE_SERVER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return entity->isInvincible;
	}, false));

	// SET_PLAYER_FROZEN_SERVER(playerSrc, toggle) -> bool
	fx::ScriptEngine::RegisterNativeHandler("SET_PLAYER_FROZEN_SERVER", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		bool toggle = context.GetArgument<bool>(1);
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		gameState->SetPlayerFrozen(client->GetNetId(), toggle);

		auto [lock, data] = gameState->ExternalGetClientData(client);
		auto playerEntity = data->playerEntity.lock();
		if (playerEntity)
		{
			playerEntity->isFrozen = toggle;
			if (toggle)
			{
				if (auto vel = playerEntity->syncTree->GetVelocity())
				{
					vel->velX = 0.0f;
					vel->velY = 0.0f;
					vel->velZ = 0.0f;
				}

				float currentPos[3] = { 0.0f, 0.0f, 0.0f };
				playerEntity->syncTree->GetPosition(currentPos);
				playerEntity->frozenCoords[0] = currentPos[0];
				playerEntity->frozenCoords[1] = currentPos[1];
				playerEntity->frozenCoords[2] = currentPos[2];
			}

			auto playerPedHandle = gameState->MakeScriptHandle(playerEntity);

			// 1. Invoke FREEZE_ENTITY_POSITION RPC on ped
			auto freezeHandler = fx::ScriptEngine::GetNativeHandler(HashString("FREEZE_ENTITY_POSITION"));
			if (freezeHandler)
			{
				fx::ScriptContextBuffer ctxBuf;
				ctxBuf.Push(playerPedHandle);
				ctxBuf.Push(toggle);
				freezeHandler(ctxBuf);
			}

			// 2. Invoke SET_PLAYER_CONTROL RPC to disable player controls
			auto controlHandler = fx::ScriptEngine::GetNativeHandler(HashString("SET_PLAYER_CONTROL"));
			if (controlHandler)
			{
				fx::ScriptContextBuffer ctxBuf;
				std::string playerStr = std::to_string(client->GetNetId());
				ctxBuf.Push(playerStr.c_str());
				ctxBuf.Push(!toggle);
				ctxBuf.Push(0);
				controlHandler(ctxBuf);
			}

			// 3. If in vehicle, freeze the vehicle too
			if (auto pvd = playerEntity->syncTree->GetPedVehicleData())
			{
				if (pvd->inVehicle && pvd->curVehicle)
				{
					auto vehEntity = gameState->GetEntity(0, pvd->curVehicle);
					if (vehEntity)
					{
						vehEntity->isFrozen = toggle;
						if (toggle)
						{
							if (auto vel = vehEntity->syncTree->GetVelocity())
							{
								vel->velX = 0.0f;
								vel->velY = 0.0f;
								vel->velZ = 0.0f;
							}

							float vehPos[3] = { 0.0f, 0.0f, 0.0f };
							vehEntity->syncTree->GetPosition(vehPos);
							vehEntity->frozenCoords[0] = vehPos[0];
							vehEntity->frozenCoords[1] = vehPos[1];
							vehEntity->frozenCoords[2] = vehPos[2];
						}

						auto vehHandle = gameState->MakeScriptHandle(vehEntity);
						if (freezeHandler)
						{
							fx::ScriptContextBuffer vehCtx;
							vehCtx.Push(vehHandle);
							vehCtx.Push(toggle);
							freezeHandler(vehCtx);
						}
					}
				}
			}
		}

		return true;
	}, false));

	// IS_PLAYER_FROZEN_SERVER(playerSrc) -> bool
	fx::ScriptEngine::RegisterNativeHandler("IS_PLAYER_FROZEN_SERVER", MakeClientFunction([](fx::ScriptContext& context, const fx::ClientSharedPtr& client)
	{
		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		return gameState->IsPlayerFrozen(client->GetNetId());
	}, false));

	// SET_ENTITY_FROZEN_SERVER(entity, toggle) -> bool
	fx::ScriptEngine::RegisterNativeHandler("SET_ENTITY_FROZEN_SERVER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		bool toggle = context.GetArgument<bool>(1);
		entity->isFrozen = toggle;

		if (toggle)
		{
			if (auto vel = entity->syncTree->GetVelocity())
			{
				vel->velX = 0.0f;
				vel->velY = 0.0f;
				vel->velZ = 0.0f;
			}

			float currentPos[3] = { 0.0f, 0.0f, 0.0f };
			entity->syncTree->GetPosition(currentPos);
			entity->frozenCoords[0] = currentPos[0];
			entity->frozenCoords[1] = currentPos[1];
			entity->frozenCoords[2] = currentPos[2];
		}

		auto resourceManager = fx::ResourceManager::GetCurrent();
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		auto gameState = instance->GetComponent<fx::ServerGameState>();
		auto entityHandle = gameState->MakeScriptHandle(entity);

		auto handler = fx::ScriptEngine::GetNativeHandler(HashString("FREEZE_ENTITY_POSITION"));
		if (handler)
		{
			fx::ScriptContextBuffer ctxBuf;
			ctxBuf.Push(entityHandle);
			ctxBuf.Push(toggle);
			handler(ctxBuf);
		}

		return true;
	}, false));

	// IS_ENTITY_FROZEN_SERVER(entity) -> bool
	fx::ScriptEngine::RegisterNativeHandler("IS_ENTITY_FROZEN_SERVER", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		return entity->isFrozen;
	}, false));

	// Aliases to existing RPC natives
	fx::ScriptEngine::RegisterNativeHandler("SET_PED_INTO_VEHICLE_SERVER", [](fx::ScriptContext& context)
	{
		auto handler = fx::ScriptEngine::GetNativeHandler(HashString("SET_PED_INTO_VEHICLE"));
		if (handler) handler(context);
	});

	fx::ScriptEngine::RegisterNativeHandler("TASK_LEAVE_VEHICLE_SERVER", [](fx::ScriptContext& context)
	{
		auto handler = fx::ScriptEngine::GetNativeHandler(HashString("TASK_LEAVE_VEHICLE"));
		if (handler) handler(context);
	});

	fx::ScriptEngine::RegisterNativeHandler("SET_PED_TO_RAGDOLL_SERVER", [](fx::ScriptContext& context)
	{
		auto handler = fx::ScriptEngine::GetNativeHandler(HashString("SET_PED_TO_RAGDOLL"));
		if (handler) handler(context);
	});

	fx::ScriptEngine::RegisterNativeHandler("CLEAR_PED_TASKS_IMMEDIATELY_SERVER", [](fx::ScriptContext& context)
	{
		auto handler = fx::ScriptEngine::GetNativeHandler(HashString("CLEAR_PED_TASKS_IMMEDIATELY"));
		if (handler) handler(context);
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_LOCK_ON_TARGET", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		int lockOnHandle = 0;

		if (auto state = entity->syncTree->GetPlaneGameState())
		{
			auto resourceManager = fx::ResourceManager::GetCurrent();

			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

			auto gameState = instance->GetComponent<fx::ServerGameState>();

			auto lockOnEntity = gameState->GetEntity(0, state->lockOnEntity);

			lockOnHandle = lockOnEntity ? gameState->MakeScriptHandle(lockOnEntity) : 0;
		}

		return lockOnHandle;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_HOMING_LOCKON_STATE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto state = entity->syncTree->GetPlaneGameState();

		return state ? state->lockOnState : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_FLIGHT_NOZZLE_POSITION", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto state = entity->syncTree->GetPlaneControl();

		return state ? state->nozzlePosition : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_SCRIPT_TASK_COMMAND", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto tree = entity->syncTree->GetPedTaskTree();

		return tree ? tree->scriptCommand : 0x811E343C;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_SCRIPT_TASK_STAGE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto tree = entity->syncTree->GetPedTaskTree();

		return tree ? tree->scriptTaskStage : 3;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_SPECIFIC_TASK_TYPE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
#ifdef STATE_FIVE
		auto tree = entity->syncTree->GetPedTaskTree();
		if (!tree)
		{
			return Is2060() ? 531 : 530;
		}

		int index = context.GetArgument<int>(1);
		if (index < 0 || index > 7)
		{
			return Is2060() ? 531 : 530;
		}

		auto& task = tree->tasks[index];
		return static_cast<int>(task.type);
#endif

		return 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_RELATIONSHIP_GROUP_HASH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto ped = entity->syncTree->GetPedAI();
		return ped ? ped->relationShip : 0;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_SPEED", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto v = entity->syncTree->GetVelocity();

		float speed = 0.0f;

		if (v)
		{
			speed = std::hypot(v->velX, v->velY, v->velZ);
		}

		return speed;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_ATTACHED_TO", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		int handle = 0;

		if (auto attachment = entity->syncTree->GetAttachment())
		{
			if (attachment->attached)
			{
				auto resman = fx::ResourceManager::GetCurrent();
				auto instance = resman->GetComponent<fx::ServerInstanceBaseRef>()->Get();
				auto gameState = instance->GetComponent<fx::ServerGameState>();

				if (auto entity = gameState->GetEntity(0, attachment->attachedTo))
				{
					handle = gameState->MakeScriptHandle(entity);
				}
			}
		}

		return handle;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_MAIN_ROTOR_HEALTH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliHealth = entity->syncTree->GetHeliHealth();

		return heliHealth ? float(heliHealth->mainRotorHealth) : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_TAIL_ROTOR_HEALTH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		fx::WarningDeprecationf<fx::ScriptDeprecations::GET_HELI_TAIL_ROTOR_HEALTH>("natives", "GET_HELI_TAIL_ROTOR_HEALTH is deprecated because there is no tail motor. Use GET_HELI_REAR_ROTOR_HEALTH instead.\n");
		auto heliHealth = entity->syncTree->GetHeliHealth();

		return heliHealth ? float(heliHealth->rearRotorHealth) : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_REAR_ROTOR_HEALTH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliHealth = entity->syncTree->GetHeliHealth();

		return heliHealth ? float(heliHealth->rearRotorHealth) : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_HELI_TAIL_BOOM_BROKEN", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliHealth = entity->syncTree->GetHeliHealth();

		return heliHealth ? heliHealth->boomBroken : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_HELI_TAIL_BOOM_BREAKABLE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliHealth = entity->syncTree->GetHeliHealth();

		return heliHealth ? heliHealth->canBoomBreak : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_BODY_HEALTH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliHealth = entity->syncTree->GetHeliHealth();

		return heliHealth ? heliHealth->bodyHealth : 1000; // Since the custom health bit wasn't trigger, we are returning the default value. 
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_GAS_TANK_HEALTH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliHealth = entity->syncTree->GetHeliHealth();

		return heliHealth ? heliHealth->gasTankHealth : 1000; // TODO: Read the gas tank health from the vehicle health datanode. 
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_ENGINE_HEALTH", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliHealth = entity->syncTree->GetHeliHealth();

		return heliHealth ? heliHealth->engineHealth : 1000; // Since the custom health bit wasn't trigger, we are returning the default value.
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_MAIN_ROTOR_DAMAGE_SCALE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliHealth = entity->syncTree->GetHeliHealth();

		return heliHealth ? heliHealth->mainRotorDamage : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_REAR_ROTOR_DAMAGE_SCALE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliHealth = entity->syncTree->GetHeliHealth();

		return heliHealth ? heliHealth->rearRotorDamage : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_TAIL_ROTOR_DAMAGE_SCALE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliHealth = entity->syncTree->GetHeliHealth();

		return heliHealth ? heliHealth->tailRotorDamage : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_DISABLE_EXPLODE_FROM_BODY_DAMAGE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliHealth = entity->syncTree->GetHeliHealth();

		return heliHealth ? heliHealth->disableExplosionFromBodyDamage : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_LANDING_GEAR_STATE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		int gearState = 0;

		if (entity->type == fx::sync::NetObjEntityType::Heli)
		{
			auto state = entity->syncTree->GetHeliControl();
			if (state->hasLandingGear)
			{
				gearState = state->landingGearState;
			}
		}
		else if (entity->type == fx::sync::NetObjEntityType::Plane)
		{
			auto state = entity->syncTree->GetPlaneGameState();
			gearState = state ? state->landingGearState : 0;
		}

		return gearState;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_YAW_CONTROL", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliControl = entity->syncTree->GetHeliControl();

		return heliControl ? heliControl->yawControl : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_PITCH_CONTROL", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliControl = entity->syncTree->GetHeliControl();

		return heliControl ? heliControl->pitchControl : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_ROLL_CONTROL", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliControl = entity->syncTree->GetHeliControl();

		return heliControl ? heliControl->rollControl : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_HELI_THROTTLE_CONTROL", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliControl = entity->syncTree->GetHeliControl();

		return heliControl ? heliControl->throttleControl : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_THRUSTER_SIDE_RCS_THROTTLE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliControl = entity->syncTree->GetHeliControl();

		return heliControl ? heliControl->thrusterSideRCSThrottle : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_THRUSTER_THROTTLE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliControl = entity->syncTree->GetHeliControl();

		return heliControl ? heliControl->thrusterThrottle : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_IS_HELI_ENGINE_RUNNING", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto heliControl = entity->syncTree->GetHeliControl();

		return heliControl ? !heliControl->engineOff : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_VEHICLE_STEERING_ANGLE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto steeringData = entity->syncTree->GetVehicleSteeringData();

		return steeringData ? steeringData->steeringAngle * (180.0f / pi) : 0.0f;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_COLLISION_DISABLED", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto scriptGameState = entity->syncTree->GetEntityScriptGameState();

		return scriptGameState ? !scriptGameState->usesCollision : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_ENTITY_POSITION_FROZEN", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto scriptGameState = entity->syncTree->GetEntityScriptGameState();

		return scriptGameState ? scriptGameState->isFixed : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_FLASH_LIGHT_ON", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto gameState = entity->syncTree->GetPedGameState();

		return gameState ? gameState->isFlashlightOn : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_PED_USING_ACTION_MODE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto gameState = entity->syncTree->GetPedGameState();

		return gameState ? gameState->actionModeEnabled : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_PED_HANDCUFFED", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto gameState = entity->syncTree->GetPedGameState();

		return gameState ? gameState->isHandcuffed : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("HAS_VEHICLE_BEEN_DAMAGED_BY_BULLETS", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto status = entity->syncTree->GetVehicleDamageStatus();

		return status ? status->damagedByBullets : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_VEHICLE_WINDOW_INTACT", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto status = entity->syncTree->GetVehicleDamageStatus();
		int index = context.GetArgument<int>(1);

		if (!status)
		{
			return false;
		}

		if (index < 0 || index > 7)
		{
			return false;
		}

		return status->anyWindowBroken ? !status->windowsState[index] : true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_BOAT_ANCHORED_AND_FROZEN", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto boatGameState = entity->syncTree->GetBoatGameState();

		return boatGameState ? boatGameState->lockedToXY : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_BOAT_WRECKED", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto boatGameState = entity->syncTree->GetBoatGameState();

#ifndef STATE_RDR3
		return boatGameState ? (boatGameState->sinkEndTime == 0.0f) : false;
#else
		return boatGameState ? boatGameState->isWrecked : false;
#endif
	}));

	fx::ScriptEngine::RegisterNativeHandler("DOES_BOAT_SINK_WHEN_WRECKED", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto boatGameState = entity->syncTree->GetBoatGameState();

		return boatGameState ? (boatGameState->wreckedAction == 2) : true;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_PED_STEALTH_MOVEMENT", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto movementGroup = entity->syncTree->GetPedMovementGroup();

		return movementGroup ? movementGroup->isStealthy : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_PED_STRAFING", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto movementGroup = entity->syncTree->GetPedMovementGroup();

		return movementGroup ? movementGroup->isStrafing : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_PED_RAGDOLL", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		auto movementGroup = entity->syncTree->GetPedMovementGroup();

		return movementGroup ? movementGroup->isRagdolling : false;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITY_FROM_STATE_BAG_NAME", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		auto entity = 0;

		// get the state bag name
		std::string bagName = context.CheckArgument<const char*>(0);

		// we only want to handle conversion for entity
		if (bagName.find("entity:") == 0)
		{
			int parsedEntityId = atoi(bagName.substr(7).c_str());

			auto entityPtr = gameState->GetEntity(0, parsedEntityId);

			if (entityPtr)
			{
				entity = gameState->MakeScriptHandle(entityPtr);
			}
		}

		context.SetResult(entity);
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_PLAYER_FROM_STATE_BAG_NAME", [](fx::ScriptContext& context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's client registry
		auto clientRegistry = instance->GetComponent<fx::ClientRegistry>();

		auto player = 0;

		// get the state bag name
		std::string bagName = context.CheckArgument<const char*>(0);

		// we only want to handle conversion for players
		if (bagName.find("player:") == 0)
		{
			int playerNetId = atoi(bagName.substr(7).c_str());

			// We don't want to return the player if they don't exist
			if (auto client = clientRegistry->GetClientByNetID(playerNetId))
			{
				player = client->GetNetId();
			};
		}

		context.SetResult(player);
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_ENTITIES_IN_RADIUS", [](fx::ScriptContext& context)
	{

		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's game state
		auto gameState = instance->GetComponent<fx::ServerGameState>();

		float checkX = context.GetArgument<float>(0);
		float checkY = context.GetArgument<float>(1);
		float checkZ = context.GetArgument<float>(2);
		float radius = context.GetArgument<float>(3);
		float squaredMaxDistance = radius * radius;
		int entityType = context.GetArgument<int>(4);
		bool sortOutput = context.GetArgument<bool>(5);
		fx::scrObject models = context.GetArgument<fx::scrObject>(6);

		std::vector<int> modelList = fx::DeserializeObject<std::vector<int>>(models);
		std::unordered_set<int> modelSet(modelList.begin(), modelList.end());

		std::vector<std::pair<float, int>> entities;
		std::shared_lock l(gameState->m_entityListMutex);

		EntityType desiredType = EntityType::NoEntity;
		if (entityType == 1)
			desiredType = EntityType::Ped;
		else if (entityType == 2)
			desiredType = EntityType::Vehicle;
		else if (entityType == 3)
			desiredType = EntityType::Object;

		for (auto& entity : gameState->m_entityList)
		{
			if (!IsEntityValid(entity) || GetEntityType(entity) != desiredType)
				continue;

			float position[3];
			entity->syncTree->GetPosition(position);

			float dx = position[0] - checkX;
			float dy = position[1] - checkY;
			float dz = position[2] - checkZ;
			float distSq = dx * dx + dy * dy + dz * dz;

			if (distSq >= squaredMaxDistance)
				continue;

			uint32_t modelHash = 0;
			entity->syncTree->GetModelHash(&modelHash);

			if (modelSet.empty() || modelSet.find(modelHash) != modelSet.end())
			{
				entities.push_back({ distSq, gameState->MakeScriptHandle(entity) });
			}
		}

		if (sortOutput)
		{
			std::sort(entities.begin(), entities.end(), [](const auto& a, const auto& b)
			{
				return a.first < b.first;
			});
		}

		std::vector<int> entityList;
		entityList.reserve(entities.size());
		for (auto& entry : entities)
		{
			entityList.push_back(entry.second);
		}

		context.SetResult(fx::SerializeObject(entityList));
	});

	fx::ScriptEngine::RegisterNativeHandler("GET_SEAT_PED_IS_USING", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		 int currentSeat = -3;
		 auto resourceManager = fx::ResourceManager::GetCurrent();
		 auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		 auto gameState = instance->GetComponent<fx::ServerGameState>();

#ifdef STATE_RDR3

		 auto pedVehicleData = entity->syncTree->GetPedVehicleData();
		 if (!pedVehicleData)
			 return -3;

		 currentSeat = pedVehicleData->curSeat != 0 ? pedVehicleData->curSeat - 2 : -3; // offset -2 just like client behaviour, if not in vehicle, return -3, works for Horses too

#else
		 auto pedGameState = entity->syncTree->GetPedGameState();
		 if (!pedGameState)
			 return -3;
		 currentSeat = pedGameState->curVehicleSeat - 2;

#endif

		 return currentSeat;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_PED_IN_ANY_VEHICLE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		 bool inVehicle = false;

#ifdef STATE_RDR3
		 auto pedVehicleData = entity->syncTree->GetPedVehicleData();
		 if (!pedVehicleData)
			 return false;

		 inVehicle = pedVehicleData->inVehicle;
#else
		 auto pedGameState = entity->syncTree->GetPedGameState();
		 if (!pedGameState)
			 return false;

		 inVehicle = pedGameState->curVehicleSeat != -1;
#endif

		 return inVehicle;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_PED_IN_VEHICLE", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		 const int vehEntity = context.GetArgument<int>(1);
		 if (!vehEntity)
			 return false;

		 int vehicleId = 0;
		 bool inVehicle = false;

#ifdef STATE_RDR3

		 auto pedVehicleData = entity->syncTree->GetPedVehicleData();
		 if (!pedVehicleData)
			 return false;

		 inVehicle = pedVehicleData->inVehicle; 
		 vehicleId = pedVehicleData->curVehicle; // 0 at first then always has a an id unlike fivem that resets to -1

#else
		 auto pedGameState = entity->syncTree->GetPedGameState();
		 if (!pedGameState)
			 return false;
		 inVehicle = pedGameState->curVehicleSeat != -1;
		 vehicleId = pedGameState->curVehicle != -1 ? pedGameState->curVehicle : 0;

#endif

		 if (!vehicleId || !inVehicle)
			 return false;

		 auto resourceManager = fx::ResourceManager::GetCurrent();
		 auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		 auto gameState = instance->GetComponent<fx::ServerGameState>();

		 auto veh = gameState->GetEntity(vehEntity);
		 if (!veh)
			 return false;

		 return vehicleId == veh->handle;
	}));

	fx::ScriptEngine::RegisterNativeHandler("GET_MOUNT", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		 auto pedVehicleData = entity->syncTree->GetPedVehicleData();
		 if (!pedVehicleData)
			 return 0;

		 const bool onHorse = pedVehicleData->onHorse;
		 const int horseId = pedVehicleData->curHorse;
		 if (!onHorse || !horseId)
			 return 0;

		 auto resourceManager = fx::ResourceManager::GetCurrent();
		 auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
		 auto gameState = instance->GetComponent<fx::ServerGameState>();

		 auto horseEntity = gameState->GetEntity(0, horseId);
		 if (!horseEntity || !IsEntityValid(horseEntity))
			 return 0;

		 const int ent = gameState->MakeScriptHandle(horseEntity);
		 return ent;
	}));

	fx::ScriptEngine::RegisterNativeHandler("IS_PED_ON_MOUNT", makeEntityFunction([](fx::ScriptContext& context, const fx::sync::SyncEntityPtr& entity)
	{
		   auto pedVehicleData = entity->syncTree->GetPedVehicleData();
		   if (!pedVehicleData)
			   return false;
			   
		   return pedVehicleData->onHorse;
	}));

	//
	// Synchronized Scene Server Natives
	//
	auto registerSceneNatives = []()
	{
		fx::ScriptEngine::RegisterNativeHandler("CREATE_SERVER_SYNCHRONIZED_SCENE", [](fx::ScriptContext& context)
		{
			auto resourceManager = fx::ResourceManager::GetCurrent();
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
			auto sceneMan = instance->GetComponent<fx::SynchronizedSceneManager>();

			if (!sceneMan.GetRef())
			{
				context.SetResult(-1);
				return;
			}

			float x = context.GetArgument<float>(0);
			float y = context.GetArgument<float>(1);
			float z = context.GetArgument<float>(2);
			float rx = context.GetArgument<float>(3);
			float ry = context.GetArgument<float>(4);
			float rz = context.GetArgument<float>(5);
			int rotOrder = context.GetArgument<int>(6);
			float duration = context.GetArgument<float>(7);
			bool isLooping = context.GetArgument<bool>(8);
			bool holdLastFrame = context.GetArgument<bool>(9);

			int sceneId = sceneMan->CreateScene(glm::vec3(x, y, z), glm::vec3(rx, ry, rz), rotOrder, duration, isLooping, holdLastFrame);
			context.SetResult(sceneId);
		});

		fx::ScriptEngine::RegisterNativeHandler("CREATE_SYNCHRONIZED_SCENE", [](fx::ScriptContext& context)
		{
			auto handler = fx::ScriptEngine::GetNativeHandler(HashString("CREATE_SERVER_SYNCHRONIZED_SCENE"));
			if (handler) handler(context);
		});

		fx::ScriptEngine::RegisterNativeHandler("ADD_PED_TO_SERVER_SYNCHRONIZED_SCENE", [](fx::ScriptContext& context)
		{
			auto resourceManager = fx::ResourceManager::GetCurrent();
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
			auto sceneMan = instance->GetComponent<fx::SynchronizedSceneManager>();
			auto gameState = instance->GetComponent<fx::ServerGameState>();

			if (!sceneMan.GetRef() || !gameState.GetRef())
			{
				context.SetResult(false);
				return;
			}

			int sceneId = context.GetArgument<int>(0);
			uint32_t pedHandle = context.GetArgument<uint32_t>(1);
			const char* animDict = context.CheckArgument<const char*>(2);
			const char* animName = context.CheckArgument<const char*>(3);
			float blendInSpeed = context.GetArgument<float>(4);
			float blendOutSpeed = context.GetArgument<float>(5);
			int flags = context.GetArgument<int>(6);
			int ragdollFlags = context.GetArgument<int>(7);
			float moverBlendInDelta = context.GetArgument<float>(8);
			int ikFlags = context.GetArgument<int>(9);

			auto entity = gameState->GetEntity(pedHandle);
			if (!entity)
			{
				context.SetResult(false);
				return;
			}

			bool res = sceneMan->AddPedToScene(sceneId, entity->handle, animDict ? animDict : "", animName ? animName : "", blendInSpeed, blendOutSpeed, flags, ragdollFlags, moverBlendInDelta, ikFlags);
			context.SetResult(res);
		});

		fx::ScriptEngine::RegisterNativeHandler("ADD_PED_TO_SYNCHRONIZED_SCENE", [](fx::ScriptContext& context)
		{
			auto handler = fx::ScriptEngine::GetNativeHandler(HashString("ADD_PED_TO_SERVER_SYNCHRONIZED_SCENE"));
			if (handler) handler(context);
		});

		fx::ScriptEngine::RegisterNativeHandler("ADD_ENTITY_TO_SERVER_SYNCHRONIZED_SCENE", [](fx::ScriptContext& context)
		{
			auto resourceManager = fx::ResourceManager::GetCurrent();
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
			auto sceneMan = instance->GetComponent<fx::SynchronizedSceneManager>();
			auto gameState = instance->GetComponent<fx::ServerGameState>();

			if (!sceneMan.GetRef() || !gameState.GetRef())
			{
				context.SetResult(false);
				return;
			}

			int sceneId = context.GetArgument<int>(0);
			uint32_t entHandle = context.GetArgument<uint32_t>(1);
			const char* animDict = context.CheckArgument<const char*>(2);
			const char* animName = context.CheckArgument<const char*>(3);
			float blendInSpeed = context.GetArgument<float>(4);
			float blendOutSpeed = context.GetArgument<float>(5);
			int flags = context.GetArgument<int>(6);

			auto entity = gameState->GetEntity(entHandle);
			if (!entity)
			{
				context.SetResult(false);
				return;
			}

			bool res = sceneMan->AddEntityToScene(sceneId, entity->handle, animDict ? animDict : "", animName ? animName : "", blendInSpeed, blendOutSpeed, flags);
			context.SetResult(res);
		});

		fx::ScriptEngine::RegisterNativeHandler("ADD_ENTITY_TO_SYNCHRONIZED_SCENE", [](fx::ScriptContext& context)
		{
			auto handler = fx::ScriptEngine::GetNativeHandler(HashString("ADD_ENTITY_TO_SERVER_SYNCHRONIZED_SCENE"));
			if (handler) handler(context);
		});

		fx::ScriptEngine::RegisterNativeHandler("START_SERVER_SYNCHRONIZED_SCENE", [](fx::ScriptContext& context)
		{
			auto resourceManager = fx::ResourceManager::GetCurrent();
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
			auto sceneMan = instance->GetComponent<fx::SynchronizedSceneManager>();

			if (!sceneMan.GetRef())
			{
				context.SetResult(false);
				return;
			}

			int sceneId = context.GetArgument<int>(0);
			float animRate = context.GetArgument<float>(1);
			if (animRate <= 0.0f)
			{
				animRate = 1.0f;
			}
			int initiatorNetId = context.GetArgument<int>(2);

			bool res = sceneMan->StartScene(sceneId, animRate, initiatorNetId);
			context.SetResult(res);
		});

		fx::ScriptEngine::RegisterNativeHandler("START_SYNCHRONIZED_SCENE", [](fx::ScriptContext& context)
		{
			auto handler = fx::ScriptEngine::GetNativeHandler(HashString("START_SERVER_SYNCHRONIZED_SCENE"));
			if (handler) handler(context);
		});

		fx::ScriptEngine::RegisterNativeHandler("STOP_SERVER_SYNCHRONIZED_SCENE", [](fx::ScriptContext& context)
		{
			auto resourceManager = fx::ResourceManager::GetCurrent();
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
			auto sceneMan = instance->GetComponent<fx::SynchronizedSceneManager>();

			if (!sceneMan.GetRef())
			{
				context.SetResult(false);
				return;
			}

			int sceneId = context.GetArgument<int>(0);
			bool res = sceneMan->StopScene(sceneId);
			context.SetResult(res);
		});

		fx::ScriptEngine::RegisterNativeHandler("STOP_SYNCHRONIZED_SCENE", [](fx::ScriptContext& context)
		{
			auto handler = fx::ScriptEngine::GetNativeHandler(HashString("STOP_SERVER_SYNCHRONIZED_SCENE"));
			if (handler) handler(context);
		});

		fx::ScriptEngine::RegisterNativeHandler("SET_SERVER_SYNCHRONIZED_SCENE_PHASE", [](fx::ScriptContext& context)
		{
			auto resourceManager = fx::ResourceManager::GetCurrent();
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
			auto sceneMan = instance->GetComponent<fx::SynchronizedSceneManager>();

			if (!sceneMan.GetRef())
			{
				context.SetResult(false);
				return;
			}

			int sceneId = context.GetArgument<int>(0);
			float phase = context.GetArgument<float>(1);
			bool res = sceneMan->SetScenePhase(sceneId, phase);
			context.SetResult(res);
		});

		fx::ScriptEngine::RegisterNativeHandler("SET_SERVER_SYNCHRONIZED_SCENE_RATE", [](fx::ScriptContext& context)
		{
			auto resourceManager = fx::ResourceManager::GetCurrent();
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
			auto sceneMan = instance->GetComponent<fx::SynchronizedSceneManager>();

			if (!sceneMan.GetRef())
			{
				context.SetResult(false);
				return;
			}

			int sceneId = context.GetArgument<int>(0);
			float rate = context.GetArgument<float>(1);
			bool res = sceneMan->SetSceneRate(sceneId, rate);
			context.SetResult(res);
		});

		fx::ScriptEngine::RegisterNativeHandler("DOES_SERVER_SYNCHRONIZED_SCENE_EXIST", [](fx::ScriptContext& context)
		{
			auto resourceManager = fx::ResourceManager::GetCurrent();
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
			auto sceneMan = instance->GetComponent<fx::SynchronizedSceneManager>();

			if (!sceneMan.GetRef())
			{
				context.SetResult(false);
				return;
			}

			int sceneId = context.GetArgument<int>(0);
			bool res = sceneMan->DoesSceneExist(sceneId);
			context.SetResult(res);
		});

		fx::ScriptEngine::RegisterNativeHandler("GET_SERVER_SYNCHRONIZED_SCENE_PHASE", [](fx::ScriptContext& context)
		{
			auto resourceManager = fx::ResourceManager::GetCurrent();
			auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();
			auto sceneMan = instance->GetComponent<fx::SynchronizedSceneManager>();

			if (!sceneMan.GetRef())
			{
				context.SetResult(0.0f);
				return;
			}

			int sceneId = context.GetArgument<int>(0);
			float phase = sceneMan->GetScenePhase(sceneId);
			context.SetResult(phase);
		});
	};

	registerSceneNatives();
}

static InitFunction initFunction([]()
{
	fx::ServerInstanceBase::OnServerCreate.Connect([](fx::ServerInstanceBase* instance)
	{
		if (!IsStateGame())
		{
			return;
		}

		Init();
	},
	INT32_MIN);
});

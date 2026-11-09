---
ns: CFX
apiset: server
---
## SET_PLAYER_HIDDEN_FROM_OTHERS

```c
BOOL SET_PLAYER_HIDDEN_FROM_OTHERS(char* playerSrc, BOOL toggle);
```

Sets whether the specified player is hidden from all other players on the server.
When hidden (ghost mode):
- The player's ped entity, current vehicle, and attached entities are immediately removed from scope for all other clients.
- Other players cannot see, hear (Mumble voice muted), or receive sync packets from the hidden player.
- The hidden player continues to see, hear, and observe the world and all other players normally.

## Parameters
* **playerSrc**: The player server ID (integer or string).
* **toggle**: Whether to hide (`true`) or reveal (`false`) the player to others.

## Return value
Returns `true` if the player client was found and updated, `false` otherwise.

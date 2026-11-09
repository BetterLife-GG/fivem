---
ns: CFX
apiset: server
---
## GET_PLAYERS_IN_SCOPE

```c
object GET_PLAYERS_IN_SCOPE(int playerNetId, BOOL includeSelf);
```

Returns an array of player network IDs that are currently in scope of the specified player.

## Parameters
* **playerNetId**: The network ID of the target player.
* **includeSelf**: Whether to include the specified player's network ID in the returned array (optional, defaults to false).

## Return value
An array of player network IDs currently within sync/networking scope of the player.

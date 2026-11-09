---
ns: CFX
apiset: server
---
## IS_PLAYER_HIDDEN_FROM_OTHERS

```c
BOOL IS_PLAYER_HIDDEN_FROM_OTHERS(char* playerSrc);
```

Returns whether the specified player is currently hidden from other players (ghost mode).

## Parameters
* **playerSrc**: The player server ID (integer or string).

## Return value
`true` if the player is currently hidden from others, `false` otherwise.

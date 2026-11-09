---
trigger: always_on
---

# STRICT NO-EDIT POLICY FOR FXSERVER CORE BINARIES (ASSET ESCROW INTEGRITY)

NEVER recompile or deploy replacement builds of `citizen-server-impl.dll`, `citizen-scripting-core.dll`, `citizen-scripting-lua.dll`, or other core engine binaries to `_artifacts`. 
Recompiling these binaries from the open-source GitHub tree lacks proprietary Cfx.re Asset Escrow decryption hooks (`pUndumpHook`, `EscrowComplianceData`, etc.), breaking all escrow-protected resources (`syntax error near '<\1>'`).

All server mechanics, native fixes, state overrides, and network patches MUST be implemented OUTSIDE core binaries:
- In standalone sidecars or external hook/plugin DLLs, or
- Directly in resource scripts (`bl-base`, `bl-vehicles`, etc.).

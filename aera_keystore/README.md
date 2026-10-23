This recovery-only entrypoint retains the pinned platform Keystore 2.0 startup,
including its linked crypto implementation, Binder services, entropy feeder,
shared-secret negotiation and temporary database path.

Its sole source difference from the entrypoint identified in `source.json` is
omitting the Android framework boot-completion watcher. Recovery never signals
that boot event. Leaving the platform library's boot-completed flag false also
preserves its guard against boot-completion-dependent garbage collection.

The `aera_keystore2` executable runs under the existing `keystore2` init service.
It neither sets global Android boot properties nor suppresses crypto watchdogs.
No platform source is rewritten at build time. When updating system/security,
review this entrypoint against the new pin; the source-fidelity test detects
any difference beyond the documented watcher omission.

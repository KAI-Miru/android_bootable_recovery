# Recovery checkpoint decision

Vold's metadata mount passes `cp_needsCheckpoint()` to `fs_mgr_do_mount()`.
That query considers an unmarked A/B slot sufficient to start checkpointing,
even when `/metadata/vold/checkpoint` is absent. For an F2FS `checkpoint=fs`
entry, fs_mgr then mounts userdata with `checkpoint=disable`. Recovery has no
Android framework boot-success sequence to commit the session. Writes can
report success yet disappear on reboot or unmount.

Recovery links the pinned platform `libvold` archive. Its external checkpoint
query is intercepted with the linker's `--wrap` mechanism. A thread-local RAII
scope limits this behavior to `fscrypt_mount_metadata_encrypted()` in recovery's
partition manager; queries from other crypto operations retain Vold's behavior.
The recovery query returns false only when an ext4/F2FS filesystem is mounted at `/metadata` and
the checkpoint record is either absent or exactly Vold's committed `0`.
Pending, malformed, unreadable, symlinked or unverifiable records, including
unavailable metadata, retain the original Vold decision. Pending transactions
therefore keep the existing deferred crypto-key behavior. Nothing commits,
deletes or rewrites an Android transaction or marks a boot slot successful.

Android's Vold/library sources and both device fstabs remain unchanged. The
wrapper is linked only into crypto-enabled recovery. `source.json` identifies
the reviewed query, caller and mount implementation; device source validation
requires a review if these pinned source identities change.

The host regression links a separate mock Vold archive through the same C++
symbol and linker option and runs the real recovery wrapper. Filesystem query
boundaries are mocked; no phone, mount, key, image build or privileged host
filesystem is used. A new image and a hardware reboot test are still needed
to validate runtime retention.

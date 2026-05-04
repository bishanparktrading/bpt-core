# Deployment

Tarball + atomic symlink flip. No containers, no Helm, no overlay networks.

## Pipeline

```
deploy/package-release.sh         build all + stage tarball
       ↓
release/bpt-core-vX.Y.Z.tgz       self-contained: bin/, share/, scripts/
       ↓ (scp to host)
       ↓
deploy/deploy.sh                  on host:
       ↓                            1. unpack to releases/vX.Y.Z/
       ↓                            2. atomic flip current/ -> releases/vX.Y.Z
       ↓                            3. on first install, seed bpt-<svc>/config/
       ↓                            4. regen systemd units from new release
       ↓                            5. systemd-analyze verify
       ↓                            6. cp units to live UNIT_DIR + daemon-reload
       ↓                            7. --restart for stop/reload/start cycle
       ↓
systemctl --user enable --now bpt-stack.target
```

## Atomic flip details

`current/` is a symlink to a versioned `releases/vX.Y.Z/` directory.

`deploy.sh` unpacks the new release to `releases/vY.Z.W/` first, then `ln -sfn`
flips `current/`. Linux resolves symlinks at `open()` time, not continuously, so
**already-running processes keep executing out of the previous release** until
their next restart. The flip is not the cutover; the systemd restart is.

## Rollback

```bash
deploy/rollback.sh
```

Single-generation yo-yo flip of `current/` ↔ `previous/`. Refuses with a clear
error if no `previous/` exists. Regenerates units from the rolled-back release's
`generate-units.sh` so any unit-shape changes also revert.

Yo-yo means rollback only goes back one step. Two-step rollback would need
`releases/` enumeration; deferred until a real incident requires it.

## Validation history

End-to-end validated 2026-04-22 evening:
- Stopped entire laptop stack, rebuilt tarball at HEAD
- Fresh `deploy.sh` → first-install bail on missing env → stage env → resume
- 7/7 C++ services active, `ps aux` confirmed `ExecStart` paths from `current/bin/`
- WS message field confirmed deployed binaries == today's code
- Built synthetic v0.2.0 tarball (same code, new label)
- Deployed v0.2.0 over v0.1.0 → `current/` → v0.2.0, `previous/` → v0.1.0 atomically
- **Running services untouched** — `/proc/$PID/exe` still pinned at v0.1.0 path during deploy
- `rollback.sh` (no `--restart`) → current ↔ previous swap clean

## Still open (per the deployment-structure memory)

- `deploy.sh --prune` flag for orphan systemd units (cleanup after service rename)
- `.github/workflows/release.yml` for auto-package on main push
- Ansible playbook for scp + remote deploy.sh (today: manual ssh + tarball copy)
- Parameterise `CRED_DIR` via `BPT_CRED_DIR` for prod hosts using `/etc/bpt/creds/`
- First-prod-host validation (everything to date is laptop-validated)

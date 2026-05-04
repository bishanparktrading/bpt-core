# `tape-host` — Tokyo EC2 instance for bpt-tape

Provisions:
- VPC + public subnet + IGW + route table (10.42.0.0/16)
- Security group: SSH from `operator_ssh_cidr`, all egress
- EC2 instance (default `t3.medium` Ubuntu 24.04 LTS) with 30 GB root EBS
- 500 GB gp3 EBS attached at `/opt/bpt/data` (separate volume so OS rebuilds don't touch tape data)
- Elastic IP (stable address across restarts)

Cloud-init formats the data EBS, mounts it, installs `rclone + zstd + jq`. App deploy is a separate step.

## Apply

```bash
# Edit backend.tf with values from _bootstrap.
terraform init

# Look up your public IP and pass it in (NEVER 0.0.0.0/0):
MY_IP=$(curl -s https://api.ipify.org)
terraform apply \
  -var "operator_ssh_cidr=${MY_IP}/32" \
  -var "ssh_public_key=$(cat ~/.ssh/id_ed25519.pub)"
```

Plan output should show ~10 resources to create. After apply, `terraform output ssh_command` gives a copy-pasteable SSH line.

## After apply: deploy the binary

The instance comes up bare. To deploy `bpt-tape`:

1. Build a release tarball locally: `bash deploy/package-release.sh`
2. `scp` the tarball to the instance: `scp release/*.tgz ubuntu@<eip>:/tmp/`
3. SSH in, untar, run `BPT_DEPLOY_ROOT=/opt/bpt deploy.sh`
4. Stage `/opt/bpt/config/active/env` (use `deploy/env/prod-recorder.env.example` as a template — the rclone-archiver creds + `BPT_TAPE_CONFIG` go here)
5. `systemctl --user enable --now bpt-recording.target`

(A future cloud-init or Ansible pass automates steps 2-5. Manual for v0.)

## Sizing

| Workload | Default | Why |
|---|---|---|
| CPU | 2 vCPU (`t3.medium`) | Tape is IO-light + JSON-pass-through; <10% steady CPU at 4-venue scale |
| RAM | 4 GB | Recorder process <1 GB; rest is filesystem cache |
| Root disk | 30 GB gp3 | OS + logs only |
| Data disk | 500 GB gp3 | ~30-60 days of compressed wslog at 4-venue scale |
| Bandwidth | n/a | gp3 baseline 125 MB/s — plenty for 120 KB/s steady writes |

Bump to `t3.large` (8 GB RAM) if you start adding venues with chatty WS feeds. Bump to `m5.large` if you see CPU credits draining (t-series is burstable).

## Cost (on-demand)

- t3.medium 24/7: ~$30/month
- 530 GB gp3 (root + data): ~$42/month
- Elastic IP (attached): $0
- Egress: tape upload to same-region S3 = $0; SSH/admin egress negligible
- **~$72/month before you turn on Reserved Instance pricing** (~$45/month with 1y RI)

## Security caveats

- SSH from a single IP is the v0 minimum. Better: switch to **SSM Session Manager** (no inbound port 22, no key pair distribution). Requires installing the SSM agent (already in Ubuntu AMIs) + IAM role on the instance.
- The instance has no IAM role attached. The `bpt-tape-archiver` IAM user's keys land in `/etc/bpt/creds/` via `deploy/env/prod-recorder.env.example`. Better: instance profile with assume-role, no static keys on the host. Future refactor.
- Root volume is encrypted (default in this module). Data volume is encrypted.
- No backup of the data EBS — by design, since data is replicated to S3 within an hour. If S3 sync breaks, treat it as a P1.

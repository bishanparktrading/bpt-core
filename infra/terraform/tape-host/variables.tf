variable "aws_region" {
  description = "Region for the recording host. Tokyo by default — colocates with HL matching engine."
  type        = string
  default     = "ap-northeast-1"
}

variable "availability_zone" {
  description = "Specific AZ within aws_region. Lock to one so EBS + EC2 stay co-located."
  type        = string
  default     = "ap-northeast-1a"
}

variable "instance_type" {
  description = "EC2 instance type. Tape is light on CPU/RAM (passive WS recorder + JSON pass-through). Bump to t3.large or m5.large if RAM headroom needed."
  type        = string
  default     = "t3.medium"
}

variable "data_disk_gb" {
  description = "Size of the EBS volume mounted at /opt/bpt/data. 500 GB holds ~30-60 days of compressed wslog at full 4-venue scale."
  type        = number
  default     = 500
}

variable "operator_ssh_cidr" {
  description = "CIDR allowed to SSH (port 22). Set to your operator IP /32. NEVER 0.0.0.0/0 in prod — switch to SSM Session Manager when ready."
  type        = string
  // No default — operator MUST supply.
}

variable "ssh_public_key" {
  description = "Operator's SSH public key for the EC2 key pair. Paste contents of ~/.ssh/id_ed25519.pub or similar."
  type        = string
  // No default — operator MUST supply.
}

variable "ami_owner" {
  description = "Owner ID for the AMI lookup. 099720109477 = Canonical (Ubuntu)."
  type        = string
  default     = "099720109477"
}

variable "ami_name_pattern" {
  description = "AMI name pattern. Defaults to the latest Ubuntu 24.04 LTS amd64 (HVM, gp3)."
  type        = string
  default     = "ubuntu/images/hvm-ssd-gp3/ubuntu-noble-24.04-amd64-server-*"
}

terraform {
  required_version = ">= 1.6"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.40"
    }
  }
  # No backend block: bootstrap runs with LOCAL state, since the S3 backend
  # it would point at doesn't exist yet. After apply, commit the resulting
  # terraform.tfstate to a private location (NOT this repo) — re-running
  # bootstrap from a fresh checkout will recreate everything otherwise.
}

provider "aws" {
  region = var.aws_region
}

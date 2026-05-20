terraform {
  required_version = ">= 1.6"

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.70"
    }
    random = {
      source  = "hashicorp/random"
      version = "~> 3.6"
    }
  }

  # State backend.
  #
  # v1: local state (terraform.tfstate file in this directory; gitignored).
  # When you have a second host or second engineer touching this:
  # migrate to S3 backend.
  #
  # backend "s3" {
  #   bucket         = "bpt-tf-state"
  #   key            = "bpt-secmaster/terraform.tfstate"
  #   region         = "ap-northeast-1"
  #   encrypt        = true
  #   dynamodb_table = "bpt-tf-locks"  # optional, for state locking
  # }
}

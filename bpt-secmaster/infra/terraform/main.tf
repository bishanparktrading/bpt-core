# Data sources — the default VPC + its subnets. We don't manage VPC
# config from this module; we just reference what's there. The default
# VPC has public subnets in every AZ, perfect for "publicly accessible
# RDS with strict SG."

data "aws_vpc" "default" {
  default = true
}

data "aws_subnets" "default" {
  filter {
    name   = "vpc-id"
    values = [data.aws_vpc.default.id]
  }
}

data "aws_caller_identity" "current" {}
data "aws_region" "current" {}

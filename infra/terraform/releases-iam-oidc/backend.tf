terraform {
  backend "s3" {
    bucket         = "bpt-tfstate-761f96"
    key            = "releases-iam-oidc/terraform.tfstate"
    region         = "ap-northeast-1"
    dynamodb_table = "bpt-tfstate-lock"
    encrypt        = true
  }
}

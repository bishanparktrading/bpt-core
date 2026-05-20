#!/usr/bin/env bash
# Build the refresh Lambda container, push to ECR, point the Lambda at it.
#
# Usage:
#   bpt-secmaster/infra/deploy.sh            # build + push + update Lambda
#   bpt-secmaster/infra/deploy.sh build-only # local docker build, no AWS calls
#
# Assumes:
#   - terraform apply has already run (ECR repo + Lambda exist)
#   - aws cli is configured (AWS_PROFILE or env vars)
#   - docker is running

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SECMASTER_DIR="$REPO_ROOT/bpt-secmaster"
TF_DIR="$SECMASTER_DIR/infra/terraform"

cd "$TF_DIR"

# Read ECR repo URL + Lambda name from Terraform outputs.
ECR_REPO=$(terraform output -raw ecr_repository_url)
LAMBDA_NAME=$(terraform output -raw lambda_function_name)
AWS_REGION=$(terraform output -raw rds_endpoint | awk -F. '{print $4}')
[[ -z "$AWS_REGION" ]] && AWS_REGION="ap-northeast-1"

IMAGE_TAG=$(date -u +%Y%m%d-%H%M%S)-$(git rev-parse --short HEAD 2>/dev/null || echo "nogit")
LOCAL_IMAGE="bpt-secmaster-refresh:$IMAGE_TAG"
REMOTE_IMAGE="$ECR_REPO:$IMAGE_TAG"
REMOTE_LATEST="$ECR_REPO:latest"

echo "==> Building image $LOCAL_IMAGE"
cd "$SECMASTER_DIR/lambda/refresh"
# --provenance=false: Lambda only accepts Docker v2 manifests, not the
# OCI manifests that buildx generates by default.
docker build --platform linux/amd64 --provenance=false -t "$LOCAL_IMAGE" .

if [[ "${1:-}" == "build-only" ]]; then
  echo "==> Build complete (build-only mode). Image: $LOCAL_IMAGE"
  exit 0
fi

echo "==> Tagging as $REMOTE_IMAGE + $REMOTE_LATEST"
docker tag "$LOCAL_IMAGE" "$REMOTE_IMAGE"
docker tag "$LOCAL_IMAGE" "$REMOTE_LATEST"

echo "==> Logging in to ECR"
aws ecr get-login-password --region "$AWS_REGION" \
  | docker login --username AWS --password-stdin "$(echo "$ECR_REPO" | cut -d/ -f1)"

echo "==> Pushing"
docker push "$REMOTE_IMAGE"
docker push "$REMOTE_LATEST"

echo "==> Updating Lambda function code"
aws lambda update-function-code \
  --region "$AWS_REGION" \
  --function-name "$LAMBDA_NAME" \
  --image-uri "$REMOTE_IMAGE" \
  --output text \
  --query 'LastUpdateStatus'

echo "==> Waiting for Lambda to settle"
aws lambda wait function-updated \
  --region "$AWS_REGION" \
  --function-name "$LAMBDA_NAME"

echo "==> Done. Image: $REMOTE_IMAGE"
echo "    Invoke: aws lambda invoke --function-name $LAMBDA_NAME --region $AWS_REGION /tmp/out.json && cat /tmp/out.json"
echo "    Logs:   aws logs tail /aws/lambda/$LAMBDA_NAME --region $AWS_REGION --follow"

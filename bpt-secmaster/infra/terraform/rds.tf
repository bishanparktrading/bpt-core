# ──────────────────────── Random password ─────────────────────────────
# Generated once, stored in Secrets Manager, never logged. Lambda + the
# trading host's bpt-refdata fetch it via secretsmanager:GetSecretValue.

resource "random_password" "db_master" {
  length  = 40
  special = true
  # Allowed: per RDS docs, only printable ASCII excluding '/', '@', '"',
  # and space. We exclude '@' (RDS rejects), and avoid other chars that
  # need shell-escaping when embedded in a DSN.
  override_special = "_-!%^*+="
}

# ──────────────────────── Secrets Manager ─────────────────────────────
# Stores a JSON blob with {username, password, host, port, dbname}.
# Standard shape that RDS itself uses when it rotates credentials —
# lets you wire RDS-native rotation in later without changing consumers.

resource "aws_secretsmanager_secret" "db" {
  name        = "${var.name_prefix}/db"
  description = "Postgres credentials for bpt-secmaster. Read by Lambda + bpt-refdata."

  # 7-day recovery window — accidentally `terraform destroy`-ing the
  # secret leaves it recoverable for a week before AWS hard-deletes it.
  recovery_window_in_days = 7
}

resource "aws_secretsmanager_secret_version" "db" {
  secret_id = aws_secretsmanager_secret.db.id
  secret_string = jsonencode({
    username = aws_db_instance.main.username
    password = random_password.db_master.result
    host     = aws_db_instance.main.address
    port     = aws_db_instance.main.port
    dbname   = aws_db_instance.main.db_name
    # Convenience: pre-built libpq DSN. Includes sslmode=require to
    # enforce TLS even if the client forgets.
    dsn = format(
      "postgresql://%s:%s@%s:%d/%s?sslmode=require",
      aws_db_instance.main.username,
      random_password.db_master.result,
      aws_db_instance.main.address,
      aws_db_instance.main.port,
      aws_db_instance.main.db_name,
    )
  })
}

# ──────────────────────── DB subnet group ─────────────────────────────

resource "aws_db_subnet_group" "main" {
  name        = "${var.name_prefix}-subnet-group"
  description = "Default VPC subnets, ${var.aws_region}"
  subnet_ids  = data.aws_subnets.default.ids
}

# ──────────────────────── Security group ──────────────────────────────
# Ingress on 5432 from:
#   - the trading host's public IP (bpt-refdata reads at startup)
#   - 0.0.0.0/0  (Lambda outbound IPs aren't static; mitigated by
#     strong password + TLS + non-superuser app role)
#
# Tightening later: put Lambda in VPC with endpoints (~$21/mo) and
# replace 0.0.0.0/0 with the Lambda SG.

resource "aws_security_group" "db" {
  name        = "${var.name_prefix}-db"
  description = "bpt-secmaster RDS Postgres ingress"
  vpc_id      = data.aws_vpc.default.id

  ingress {
    description = "Trading host (bpt-refdata startup reads)"
    from_port   = 5432
    to_port     = 5432
    protocol    = "tcp"
    cidr_blocks = [var.trading_host_cidr]
  }

  ingress {
    description = "Lambda (rotating egress IPs); mitigated by TLS + strong password + app-role least-privilege"
    from_port   = 5432
    to_port     = 5432
    protocol    = "tcp"
    cidr_blocks = ["0.0.0.0/0"]
  }

  egress {
    description = "no outbound needed; allow all for postgres replication / metadata sync futures"
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }
}

# ──────────────────────── RDS instance ────────────────────────────────

resource "aws_db_instance" "main" {
  identifier     = var.name_prefix
  engine         = "postgres"
  engine_version = "16.6"

  instance_class        = var.db_instance_class
  allocated_storage     = var.db_allocated_storage_gb
  max_allocated_storage = 100 # auto-grow ceiling
  storage_type          = "gp3"
  storage_encrypted     = true # default-on for db.t4g.micro

  db_name  = var.db_name
  username = var.db_master_username
  password = random_password.db_master.result

  db_subnet_group_name   = aws_db_subnet_group.main.name
  vpc_security_group_ids = [aws_security_group.db.id]
  publicly_accessible    = true

  backup_retention_period = var.db_backup_retention_days
  backup_window           = "16:00-17:00" # UTC; off-peak for Asia trading
  maintenance_window      = "sun:17:00-sun:18:00"

  # Cost / Free Tier guardrails.
  multi_az                            = false # Multi-AZ would double cost
  performance_insights_enabled        = false
  monitoring_interval                 = 0    # Enhanced monitoring is paid
  deletion_protection                 = true # prevents accidental terraform destroy
  iam_database_authentication_enabled = true # optional path to passwordless reads

  # Apply changes immediately rather than waiting for maintenance window.
  # OK for dev; switch to false in prod once trading sessions are running.
  apply_immediately = true

  # Final snapshot on destroy — keeps data recoverable even after a
  # bad `terraform destroy`.
  skip_final_snapshot       = false
  final_snapshot_identifier = "${var.name_prefix}-final-snapshot"

  lifecycle {
    # Password rotation happens via Secrets Manager, not Terraform.
    # Don't drift-correct the password if RDS-native rotation flips it.
    ignore_changes = [password]
  }
}

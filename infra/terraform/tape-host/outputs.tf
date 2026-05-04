output "instance_id" {
  description = "EC2 instance ID."
  value       = aws_instance.tape.id
}

output "public_ip" {
  description = "Stable Elastic IP. SSH here."
  value       = aws_eip.tape.public_ip
}

output "ssh_command" {
  description = "Convenience: copy/paste to SSH in. Assumes the matching private key is at ~/.ssh/id_ed25519."
  value       = "ssh -i ~/.ssh/id_ed25519 ubuntu@${aws_eip.tape.public_ip}"
}

output "data_volume_id" {
  description = "EBS volume ID for the data disk. Snapshot this before any risky operation."
  value       = aws_ebs_volume.data.id
}

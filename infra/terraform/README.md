# Terraform deployment

Variables are defined in variables.tf.  
Run:

```bash
cd infra/terraform
terraform init
terraform plan  -var-file=myenv.tfvars
terraform apply -var-file=myenv.tfvars
```
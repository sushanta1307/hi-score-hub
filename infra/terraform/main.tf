###############################################################################
# Terraform — Global Leaderboard bootstrap on Oracle Cloud Infrastructure
###############################################################################

terraform {
  required_version = ">= 1.5.0"

  required_providers {
    oci = {
      source  = "oracle/oci"
      version = "5.38.0"
    }
  }
}

###############################################################################
# Provider
###############################################################################
provider "oci" {
  tenancy_ocid      = var.tenancy_ocid
  user_ocid         = var.user_ocid
  fingerprint       = var.fingerprint
  private_key_path  = var.private_key_path
  region            = var.region
}

###############################################################################
# Locals & Tags
###############################################################################
locals {
  tags = {
    project = "global-leaderboard"
    env     = var.environment
  }
}

###############################################################################
# Data Sources
###############################################################################
# Fetch the first Availability Domain in the chosen region
data "oci_identity_availability_domains" "ads" {
  compartment_id = var.tenancy_ocid
}

###############################################################################
# Networking
###############################################################################
# 1. VCN
resource "oci_core_vcn" "gl_vcn" {
  cidr_block     = var.vcn_cidr
  compartment_id = var.compartment_ocid
  display_name   = "gl-vcn"

  freeform_tags  = local.tags
}

# 2. Internet Gateway
resource "oci_core_internet_gateway" "gl_igw" {
  compartment_id = var.compartment_ocid
  vcn_id         = oci_core_vcn.gl_vcn.id
  display_name   = "gl-igw"
  enabled     = true

  freeform_tags  = local.tags
}

# 3. Route Table
resource "oci_core_route_table" "gl_rt" {
  compartment_id = var.compartment_ocid
  vcn_id         = oci_core_vcn.gl_vcn.id
  display_name   = "gl-rt"

  # ---- route rule block (repeat for more rules) ----
  route_rules {
    destination       = "0.0.0.0/0"
    destination_type  = "CIDR_BLOCK"
    network_entity_id = oci_core_internet_gateway.gl_igw.id
    description       = "Internet egress"
  }

  freeform_tags = local.tags
}

# 4. Security List
resource "oci_core_security_list" "gl_sl" {
  compartment_id = var.compartment_ocid
  vcn_id         = oci_core_vcn.gl_vcn.id
  display_name   = "gl-security-list"

  # ---------- EGRESS (allow all) ------------------------------------------
  egress_security_rules {
    protocol          = "all"
    destination       = "0.0.0.0/0"
    destination_type  = "CIDR_BLOCK"
    description       = "Allow all egress"
  }

  # ---------- INGRESS RULES ----------------------------------------------

  # SSH
  ingress_security_rules {
    protocol     = "6"
    source       = "0.0.0.0/0"
    source_type  = "CIDR_BLOCK"
    description  = "SSH"

    tcp_options {
      min = 22
      max = 22
    }
  }

  # Kafka PLAINTEXT 9092
  ingress_security_rules {
    protocol     = "6"
    source       = "0.0.0.0/0"
    source_type  = "CIDR_BLOCK"
    description  = "Kafka"

    tcp_options {
      min = 9092
      max = 9092
    }
  }

  # Kafka-UI 8080 & Schema-Registry 8081
  ingress_security_rules {
    protocol     = "6"
    source       = "0.0.0.0/0"
    source_type  = "CIDR_BLOCK"
    description  = "HTTP UIs"

    tcp_options {
      min = 8080
      max = 8081
    }
  }

  # MySQL 3306
  ingress_security_rules {
    protocol     = "6"
    source       = "0.0.0.0/0"
    source_type  = "CIDR_BLOCK"
    description  = "MySQL"

    tcp_options {
      min = 3306
      max = 3306
    }
  }

  # Cassandra CQL 9042
  ingress_security_rules {
    protocol     = "6"
    source       = "0.0.0.0/0"
    source_type  = "CIDR_BLOCK"
    description  = "Cassandra CQL"

    tcp_options {
      min = 9042
      max = 9042
    }
  }

  freeform_tags = local.tags
}

# 5. Public Subnet
resource "oci_core_subnet" "gl_public_subnet" {
  cidr_block              = var.public_subnet_cidr
  compartment_id          = var.compartment_ocid
  vcn_id                  = oci_core_vcn.gl_vcn.id
  display_name            = "gl-public-subnet"
  availability_domain     = data.oci_identity_availability_domains.ads.availability_domains[0].name
  route_table_id          = oci_core_route_table.gl_rt.id
  prohibit_public_ip_on_vnic = false
  dns_label               = "glpub"
  security_list_ids       = [oci_core_security_list.gl_sl.id]

  freeform_tags  = local.tags
}

###############################################################################
# Compute Instance (single node running full Podman Compose stack)
###############################################################################
resource "oci_core_instance" "gl_node" {
  compartment_id      = var.compartment_ocid
  availability_domain = data.oci_identity_availability_domains.ads.availability_domains[0].name
  display_name        = "gl-node-1"
  shape               = "VM.Standard3.Flex"

  shape_config {
    memory_in_gbs = 8
    ocpus         = 2
  }

  create_vnic_details {
    subnet_id           = oci_core_subnet.gl_public_subnet.id
    assign_public_ip    = true        # keep or drop as you prefer
    display_name        = "gl-node-1-vnic"
    hostname_label      = "glnode1"
  }

  metadata = {
    ssh_authorized_keys = file(var.ssh_public_key_path)
    user_data           = base64encode(templatefile("${path.module}/scripts/cloud-init.sh", {
      git_repo_url = var.git_repo_url,
      git_branch   = var.git_repo_branch
    }))
  }

  freeform_tags = local.tags
}

###############################################################################
# Outputs
###############################################################################
output "instance_public_ip" {
  description = "Public IP of the leaderboard node"
  value       = oci_core_instance.gl_node.public_ip
}

output "kafka_ui_url" {
  description = "URL for Kafka-UI once the stack is up"
  value       = "http://${oci_core_instance.gl_node.public_ip}:8080"
}
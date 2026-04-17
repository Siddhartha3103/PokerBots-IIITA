#!/bin/bash
set -euo pipefail

# Submission package ships with a prebuilt binary.
# Keep build step deterministic for environments without full toolchain.
chmod +x ./pokerbot

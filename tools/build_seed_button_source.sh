#!/usr/bin/env bash
# Copy the platform-loadable blank ordinary-form source for runtime oracle scripts.
# Runtime scripts add a Button in ПриОткрытии (see platform_property_proof.sh).
set -euo pipefail

src_tree="work/oracle-runtime/seed-button-source"
rm -rf "$src_tree"
cp -R work/oracle-runtime/blank-source/. "$src_tree/"
echo "seed-button-source ready (blank-source copy): $src_tree/root.xml"

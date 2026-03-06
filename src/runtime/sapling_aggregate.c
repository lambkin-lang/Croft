/*
 * sapling_aggregate.c - compatibility aggregate for the legacy sapling target
 *
 * The actual implementation now lives in narrower artifacts:
 * sapling_core, sapling_runner_core, sapling_runner_host, and the WASI
 * adapters. The aggregate target remains so existing build consumers can link
 * the full kitchen-sink profile while Lambkin grows more precise target
 * selection.
 *
 * SPDX-License-Identifier: MIT
 */

int sapling_aggregate_anchor(void)
{
    return 0;
}

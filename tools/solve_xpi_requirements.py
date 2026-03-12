#!/usr/bin/env python3
"""
Heuristic XPI solver prototype.

This models the Lambkin-side requirement distillation step against
Croft's emitted `croft-xpi.json`. It is intentionally simple:

- a manifest chooses an XPI family entrypoint
- the family contributes hard requirements plus open slots
- the manifest contributes extra required bundles and slot preferences
- the solver greedily chooses one bundle per open slot

The point is not optimality. The point is to pressure-test whether the
current XPI graph is sufficient for a future solver to make coherent
choices without inventing new metadata categories.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def parse_manifest(path: Path) -> dict:
    doc = json.loads(path.read_text(encoding="utf-8"))
    solver_request = doc.get("solver_request") or {}
    return {
        "schema": doc.get("schema"),
        "name": doc.get("name"),
        "kind": doc.get("kind"),
        "applicability": doc.get("applicability"),
        "guest": doc.get("guest") or {},
        "contracts": doc.get("contracts") or {},
        "solver_request": solver_request,
        "entrypoint_family": solver_request.get("entrypoint_family"),
        "require_bundle": list(solver_request.get("require_bundles") or []),
        "expanded_path": list(solver_request.get("expanded_paths") or []),
        "prefer_slot_bundle": {
            key: list(value or [])
            for key, value in (solver_request.get("prefer_slot_bundles") or {}).items()
        },
    }


def collect_recursive_bundle_requires(bundle_name: str, bundles: dict[str, dict], out: set[str]) -> None:
    if bundle_name in out:
        return
    out.add(bundle_name)
    bundle = bundles.get(bundle_name)
    if not bundle:
        return
    for required in bundle.get("requires_bundles", []):
        collect_recursive_bundle_requires(required, bundles, out)


def bundle_conflicts(selected: set[str], candidate: str, bundles: dict[str, dict]) -> bool:
    candidate_bundle = bundles.get(candidate, {})
    candidate_conflicts = set(candidate_bundle.get("conflicts_with", []))
    for selected_name in selected:
        selected_bundle = bundles.get(selected_name, {})
        selected_conflicts = set(selected_bundle.get("conflicts_with", []))
        if candidate in selected_conflicts or selected_name in candidate_conflicts:
            return True
    return False


def collect_bundle_field(bundle_names: set[str], bundles: dict[str, dict], field: str) -> list[str]:
    values: set[str] = set()
    for bundle_name in bundle_names:
        bundle = bundles.get(bundle_name, {})
        values.update(bundle.get(field, []))
    return sorted(values)


def collect_manifest_applicability_traits(xpi: dict, applicability: str | None) -> set[str]:
    if not applicability:
        return set()
    if applicability == "host-neutral":
        return {"host-neutral"}
    if not applicability.startswith("current-machine"):
        return {applicability}

    context_traits = set((xpi.get("context") or {}).get("current_machine_traits", []))
    traits = {"current-machine"}
    traits.update(t for t in context_traits if t not in {"current-machine", "host-neutral"})
    if "windowed" in applicability:
        traits.add("windowed")
    if "macos" in applicability:
        traits.update({"macos", "unix"})
    if "unix" in applicability:
        traits.add("unix")
    return traits


def applicability_compatible(environment_traits: set[str], bundle_traits: list[str]) -> bool:
    if not bundle_traits:
        return True
    candidate = set(bundle_traits)
    if "host-neutral" in candidate:
        return True
    if not environment_traits:
        return True
    return candidate.issubset(environment_traits)


def choose_slot_bundle(
    slot_name: str,
    selected: set[str],
    bundles: dict[str, dict],
    slots: dict[str, dict],
    preferences: dict[str, list[str]],
    environment_traits: set[str],
) -> tuple[str, list[str]]:
    slot = slots.get(slot_name)
    if not slot:
        raise ValueError(f"unknown slot: {slot_name}")

    candidates = list(slot.get("bundles", []))
    if not candidates:
        raise ValueError(f"slot {slot_name!r} has no candidate bundles")

    preferred = preferences.get(slot_name, [])

    def score(bundle_name: str) -> tuple[int, int, str]:
        bundle = bundles.get(bundle_name, {})
        support = bundle.get("support_status") or ""
        if bundle_name in preferred:
            preference_rank = preferred.index(bundle_name)
        else:
            preference_rank = len(preferred) + 100
        support_rank = 0 if support == "host-implemented-subset" else 1
        return (preference_rank, support_rank, bundle_name)

    viable = []
    rejected = []
    for bundle_name in candidates:
        bundle = bundles.get(bundle_name)
        if not bundle:
            rejected.append(f"{bundle_name}: missing bundle entry")
            continue
        if bundle.get("support_status") == "stub":
            rejected.append(f"{bundle_name}: stub support")
            continue
        if not applicability_compatible(environment_traits, bundle.get("applicability_traits", [])):
            rejected.append(
                f"{bundle_name}: applicability {bundle.get('applicability_traits', [])} "
                f"not compatible with {sorted(environment_traits)}"
            )
            continue
        if bundle_conflicts(selected, bundle_name, bundles):
            rejected.append(f"{bundle_name}: conflicts with current selection")
            continue
        viable.append(bundle_name)

    if not viable:
        raise ValueError(f"no viable bundles for slot {slot_name}: {'; '.join(rejected)}")

    chosen = sorted(viable, key=score)[0]
    rationale = [f"slot {slot_name}: chose {chosen} from {', '.join(viable)}"]
    if preferred:
        rationale.append(
            f"slot {slot_name}: preference order {', '.join(preferred)}"
        )
    return chosen, rationale


def solve(xpi: dict, manifest: dict) -> dict:
    bundles = {bundle["name"]: bundle for bundle in xpi.get("bundles", [])}
    slots = {slot["name"]: slot for slot in xpi.get("slots", [])}
    entrypoints = {
        entry["name"]: entry for entry in xpi.get("entrypoints", [])
    }

    family_name = manifest.get("entrypoint_family")
    if not family_name:
        raise ValueError("manifest is missing entrypoint-family")
    family = entrypoints.get(str(family_name))
    if not family:
        raise ValueError(f"unknown XPI family entrypoint: {family_name}")

    environment_traits = set(family.get("applicability_traits", []))
    environment_traits.update(
        collect_manifest_applicability_traits(xpi, manifest.get("applicability"))
    )
    environment_traits.discard("host-neutral")

    selected_bundles: set[str] = set()
    rationale: list[str] = [f"family entrypoint: {family_name}"]
    if environment_traits:
        rationale.append(
            "environment traits: " + ", ".join(sorted(environment_traits))
        )

    for bundle_name in family.get("requires_bundles", []):
        collect_recursive_bundle_requires(bundle_name, bundles, selected_bundles)
    for bundle_name in manifest.get("require_bundle", []):
        collect_recursive_bundle_requires(bundle_name, bundles, selected_bundles)

    selected_slot_bindings = list(family.get("selected_slot_bindings", []))
    for binding in selected_slot_bindings:
        collect_recursive_bundle_requires(binding["bundle"], bundles, selected_bundles)

    open_slots = list(family.get("open_slots", []))
    preferences = manifest.get("prefer_slot_bundle", {})

    for slot_name in open_slots:
        chosen, slot_rationale = choose_slot_bundle(
            slot_name,
            selected_bundles,
            bundles,
            slots,
            preferences,
            environment_traits,
        )
        rationale.extend(slot_rationale)
        collect_recursive_bundle_requires(chosen, bundles, selected_bundles)
        selected_slot_bindings.append({"slot": slot_name, "bundle": chosen})

    unresolved_slots = []
    for slot_name in open_slots:
        if not any(binding["slot"] == slot_name for binding in selected_slot_bindings):
            unresolved_slots.append(slot_name)

    provider_artifacts = collect_bundle_field(selected_bundles, bundles, "artifacts")
    shared_substrates = collect_bundle_field(selected_bundles, bundles, "substrates")
    helper_interfaces = collect_bundle_field(selected_bundles, bundles, "helper_interfaces")
    declared_worlds = collect_bundle_field(selected_bundles, bundles, "declared_worlds")
    expanded_surfaces = collect_bundle_field(selected_bundles, bundles, "expanded_surfaces")

    return {
        "family": family_name,
        "manifest": manifest.get("name"),
        "schema": manifest.get("schema"),
        "context": xpi.get("context", {}),
        "applicability": manifest.get("applicability") or family.get("applicability"),
        "applicability_traits": sorted(environment_traits),
        "requires_bundles": sorted(selected_bundles),
        "provider_artifacts": provider_artifacts,
        "shared_substrates": shared_substrates,
        "helper_interfaces": helper_interfaces,
        "declared_worlds": declared_worlds,
        "expanded_surfaces": expanded_surfaces,
        "selected_slot_bindings": selected_slot_bindings,
        "unresolved_slots": unresolved_slots,
        "expanded_paths": manifest.get("expanded_path", []),
        "guest": manifest.get("guest", {}),
        "contracts": manifest.get("contracts", {}),
        "data_contract": (manifest.get("contracts") or {}).get("data") or manifest.get("data_contract"),
        "view_contract": (manifest.get("contracts") or {}).get("view") or manifest.get("view_contract"),
        "rationale": rationale,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--xpi", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--expect-family")
    parser.add_argument("--expect-slot", action="append", default=[])
    parser.add_argument("--expect-bundle", action="append", default=[])
    parser.add_argument("--expect-artifact", action="append", default=[])
    parser.add_argument("--output")
    args = parser.parse_args()

    xpi = json.loads(Path(args.xpi).read_text(encoding="utf-8"))
    manifest = parse_manifest(Path(args.manifest))
    plan = solve(xpi, manifest)

    if args.expect_family and plan["family"] != args.expect_family:
        print(
            f"expected family {args.expect_family!r}, got {plan['family']!r}",
            file=sys.stderr,
        )
        return 1

    selected_slots = {
        f"{binding['slot']}={binding['bundle']}" for binding in plan["selected_slot_bindings"]
    }
    for expected in args.expect_slot:
        if expected not in selected_slots:
            print(f"missing expected slot binding {expected!r}", file=sys.stderr)
            return 1

    selected_bundles = set(plan["requires_bundles"])
    for expected in args.expect_bundle:
        if expected not in selected_bundles:
            print(f"missing expected bundle {expected!r}", file=sys.stderr)
            return 1

    provider_artifacts = set(plan["provider_artifacts"])
    for expected in args.expect_artifact:
        if expected not in provider_artifacts:
            print(f"missing expected artifact {expected!r}", file=sys.stderr)
            return 1

    output = json.dumps(plan, indent=2, sort_keys=True)
    if args.output:
        Path(args.output).write_text(output + "\n", encoding="utf-8")
    else:
        print(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
